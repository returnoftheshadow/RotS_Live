#include "../account_management.h"
#include "../db.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern struct player_index_element* player_table;
extern int top_of_p_table;

namespace {

class ScopedPlayerTableEntry {
public:
    ScopedPlayerTableEntry()
        : m_previous_player_table(player_table)
        , m_previous_top_of_p_table(top_of_p_table)
    {
        player_table = new player_index_element[1] {};
        top_of_p_table = 0;
        player_table[0].name = strdup("aragorn");
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

void write_file(const std::string& path, const std::string& contents)
{
    FILE* file = fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(fwrite(contents.data(), sizeof(char), contents.size(), file), contents.size());
    ASSERT_EQ(fclose(file), 0);
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

TEST(DbLoader, LoadsExploitRecordsFromAccountSnapshotWhenRuntimeFileIsMissing) {
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

    write_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "legacy-player-data");
    const exploit_record expected_record = make_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20);
    write_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), serialize_record(expected_record));

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010103, &migration, &error_message)) << error_message;
    ASSERT_EQ(unlink(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str()), 0);

    std::vector<exploit_record> records;
    ASSERT_TRUE(load_exploit_records_for_character(temp_directory.path(), "aragorn", &records, &error_message)) << error_message;
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].type, expected_record.type);
    EXPECT_STREQ(records[0].chtime, expected_record.chtime);
    EXPECT_STREQ(records[0].chVictimName, expected_record.chVictimName);
    EXPECT_EQ(records[0].iIntParam, expected_record.iIntParam);
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

    write_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "legacy-player-data");
    const exploit_record existing_record = make_record(EXPLOIT_LEVEL, "Mon Jan  1 00:00:00 2024", "level", 10, 0, 20);
    write_file(account::legacy_exploits_file_path(temp_directory.path(), "aragorn"), serialize_record(existing_record));

    account::CharacterMigrationData migration;
    ASSERT_TRUE(account::migrate_legacy_character_by_name(temp_directory.path(), "alpha-admin", "aragorn", 1700010103, &migration, &error_message)) << error_message;
    ASSERT_EQ(unlink(account::legacy_exploits_file_path(temp_directory.path(), "aragorn").c_str()), 0);

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

    write_file(account::legacy_player_file_path(temp_directory.path(), "aragorn"), "legacy-player-data");
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
