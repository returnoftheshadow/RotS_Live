#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../pkill.h"
#include "../protos.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

extern struct room_data world;
extern struct player_index_element* player_table;
extern int top_of_p_table;
extern char world_map[];

int Crash_report_unrentables(struct char_data* ch, struct char_data* recep, const struct obj_data* obj);
void list_simple_proto(const struct char_data* ch, const struct char_data* mob);
void list_proto(const struct char_data* ch, const struct char_data* mob);
int load_zone(struct char_data* ch, const char* arg);
void free_zone(struct char_data* ch);
void symbol_to_map(int x, int y, int symb);
int pkill_update_file(const char* file, PKILL pkills[], int n);
void list_zone(const struct char_data* ch);
ACMD(do_fame);
ACMD(do_shape);
ACMD(do_say);
int create_room(struct char_data* ch, const char* arg);
int append_room(struct char_data* ch, const char* arg);
int append_object(struct char_data* ch, char* arg);
ACMD(do_emote);
extern PKILL* pkill_tab;
extern int pkill_tab_len;

namespace {

// The length of typed text the shaping editors accept: one whole input line.
constexpr int TYPED_LINE_LENGTH = MAX_INPUT_LENGTH;

// A descriptor collecting what a test character is sent, in its small or large output buffer.
class CapturingDescriptor {
public:
    CapturingDescriptor()
    {
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
    }

    ~CapturingDescriptor()
    {
        if (m_descriptor.large_outbuf != nullptr) {
            free(m_descriptor.large_outbuf->text);
            free(m_descriptor.large_outbuf);
        }
    }

    CapturingDescriptor(const CapturingDescriptor&) = delete;
    CapturingDescriptor& operator=(const CapturingDescriptor&) = delete;

    descriptor_data* get() { return &m_descriptor; }
    std::string output() const { return std::string(m_descriptor.output); }

private:
    // The descriptor a test character's desc points at.
    descriptor_data m_descriptor {};
};

// Makes sure room 0 exists, so a character standing in it can see and be seen.
void ensure_room_zero()
{
    if (room_data::BASE_WORLD == nullptr) {
        world.create_bulk(1);
    }
}

// A uniquely named folder under the system temporary folder, removed with its contents on
// destruction. path() is empty, with a test failure reported, if none could be made.
class ScopedTemporaryFolder {
public:
    ScopedTemporaryFolder()
    {
        std::error_code error;
        const std::filesystem::path temporary_root = std::filesystem::temp_directory_path(error);
        if (error) {
            ADD_FAILURE() << "no system temporary folder: " << error.message();
            return;
        }
        std::random_device random_source;
        constexpr int MAXIMUM_ATTEMPTS = 16;
        for (int attempt = 0; attempt < MAXIMUM_ATTEMPTS && m_path.empty(); ++attempt) {
            const std::filesystem::path candidate = temporary_root / ("rots-bounds-" + std::to_string(random_source()));
            if (std::filesystem::create_directory(candidate, error)) {
                m_path = candidate;
            }
        }
        if (m_path.empty()) {
            ADD_FAILURE() << "could not make a temporary folder under " << temporary_root.string();
        }
    }

    ~ScopedTemporaryFolder()
    {
        if (!m_path.empty()) {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }
    }

    ScopedTemporaryFolder(const ScopedTemporaryFolder&) = delete;
    ScopedTemporaryFolder& operator=(const ScopedTemporaryFolder&) = delete;

    const std::filesystem::path& path() const { return m_path; }

private:
    // The folder made; empty when making it failed.
    std::filesystem::path m_path;
};

// Replaces the player index with one entry per name, idnums 1001, 1002 and so on, and puts the
// previous index back on destruction.
class ScopedPlayerIndex {
public:
    explicit ScopedPlayerIndex(const std::vector<std::string>& names)
        : m_names(names)
        , m_entries(names.size())
        , m_previous_player_table(player_table)
        , m_previous_top_of_p_table(top_of_p_table)
    {
        for (size_t index = 0; index < m_names.size(); ++index) {
            m_entries[index].name = &m_names[index][0];
            m_entries[index].idnum = 1001 + static_cast<long>(index);
            m_entries[index].level = 20;
            m_entries[index].race = RACE_HUMAN;
            m_entries[index].rank = PKILL_UNRANKED;
            m_entries[index].totalrank = PKILL_UNRANKED;
        }
        player_table = m_entries.data();
        top_of_p_table = static_cast<int>(m_entries.size()) - 1;
    }

    ~ScopedPlayerIndex()
    {
        player_table = m_previous_player_table;
        top_of_p_table = m_previous_top_of_p_table;
    }

    ScopedPlayerIndex(const ScopedPlayerIndex&) = delete;
    ScopedPlayerIndex& operator=(const ScopedPlayerIndex&) = delete;

private:
    // The names the entries point at; owned here so the index never frees them.
    std::vector<std::string> m_names;
    // The index the game reads while this object lives.
    std::vector<player_index_element> m_entries;
    // The index in place before this one.
    player_index_element* m_previous_player_table;
    // top_of_p_table before this index replaced it.
    int m_previous_top_of_p_table;
};

/* ---------------------------------------------------------------- innkeeper refusal */

// A player offering an unrentable item to an innkeeper, both standing in room 0.
class InnkeeperRefusalTest : public testing::Test {
protected:
    void SetUp() override
    {
        ensure_room_zero();
        clear_char(&m_player, MOB_VOID);
        m_player.player.name = m_player_name;
        m_player.player.level = 10;
        m_player.in_room = 0;
        m_player.desc = m_descriptor.get();
        m_descriptor.get()->character = &m_player;

        clear_char(&m_innkeeper, MOB_VOID);
        m_innkeeper.player.name = m_innkeeper_name;
        m_innkeeper.player.short_descr = m_innkeeper_short;
        m_innkeeper.in_room = 0;

        clear_object(&m_item);
        m_item.item_number = -1; /* no prototype: never rentable */
        m_item.in_room = NOWHERE;
    }

    // The refusal the player is sent for an item called <short_description>.
    std::string refusal_for(std::string short_description)
    {
        m_item.short_description = &short_description[0];
        EXPECT_EQ(Crash_report_unrentables(&m_player, &m_innkeeper, &m_item), 1);
        m_item.short_description = nullptr;
        return m_descriptor.output();
    }

    // Collects what the player is told.
    CapturingDescriptor m_descriptor;
    // Storage for the player's name.
    char m_player_name[16] = "Offerer";
    // Storage for the innkeeper's name.
    char m_innkeeper_name[16] = "innkeeper";
    // Storage for the innkeeper's short description.
    char m_innkeeper_short[16] = "the innkeeper";
    // The player offering.
    char_data m_player {};
    // The innkeeper refusing.
    char_data m_innkeeper {};
    // The unrentable item offered.
    obj_data m_item {};
};

TEST_F(InnkeeperRefusalTest, NamesAnOrdinaryItemAsBefore)
{
    const std::string output = refusal_for("a wooden key");

    EXPECT_NE(output.find("tells you, 'You cannot store a wooden key.'"), std::string::npos) << output;
}

TEST_F(InnkeeperRefusalTest, NamesAnItemWithALongShortDescriptionInFull)
{
    const std::string long_name(200, 'k');

    const std::string output = refusal_for(long_name);

    EXPECT_NE(output.find("tells you, 'You cannot store " + long_name + ".'"), std::string::npos) << output;
}

/* ---------------------------------------------------------------- shaping editors */

// A builder looking at a mob prototype in the mob editor.
class MobEditorListingTest : public testing::Test {
protected:
    void SetUp() override
    {
        clear_char(&m_builder, MOB_VOID);
        m_builder.player.name = m_builder_name;
        m_builder.player.level = LEVEL_IMMORT;
        m_builder.desc = m_descriptor.get();
        m_descriptor.get()->character = &m_builder;

        clear_char(&m_mob, MOB_VOID);
        m_mob.player.long_descr = m_empty_text;
        m_mob.player.description = m_empty_text;
    }

    // Collects what the builder is shown.
    CapturingDescriptor m_descriptor;
    // Storage for the builder's name.
    char m_builder_name[16] = "Builder";
    // An empty string for the descriptions the listing prints whole.
    char m_empty_text[1] = "";
    // The builder.
    char_data m_builder {};
    // The prototype being listed.
    char_data m_mob {};
};

TEST_F(MobEditorListingTest, SimpleListingShowsATypedLineAliasAndShortDescription)
{
    std::string alias(TYPED_LINE_LENGTH, 'a');
    std::string short_description(TYPED_LINE_LENGTH, 's');
    m_mob.player.name = &alias[0];
    m_mob.player.short_descr = &short_description[0];

    list_simple_proto(&m_builder, &m_mob);

    const std::string output = m_descriptor.output();
    EXPECT_NE(output.find("(1) alias(es)         :" + alias + "\n\r"), std::string::npos);
    EXPECT_NE(output.find("(2) reference description  :" + short_description + "\n\r"), std::string::npos);
}

TEST_F(MobEditorListingTest, FullListingShowsATypedLineAliasAndShortDescription)
{
    std::string alias(TYPED_LINE_LENGTH, 'a');
    std::string short_description(TYPED_LINE_LENGTH, 's');
    m_mob.player.name = &alias[0];
    m_mob.player.short_descr = &short_description[0];

    list_proto(&m_builder, &m_mob);

    const std::string output = m_descriptor.output();
    EXPECT_NE(output.find("(1) alias(es)         :" + alias + "\n\r"), std::string::npos);
    EXPECT_NE(output.find("(2) reference description  :" + short_description + "\n\r"), std::string::npos);
}

// A builder loading zone 45 into the zone editor from a scratch world/zon folder.
class ZoneEditorLoadTest : public testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_FALSE(m_folder.path().empty());
        std::error_code error;
        std::filesystem::create_directories(m_folder.path() / "world/zon", error);
        ASSERT_FALSE(error) << error.message();
        m_previous_folder = std::filesystem::current_path(error);
        ASSERT_FALSE(error) << error.message();
        std::filesystem::current_path(m_folder.path(), error);
        ASSERT_FALSE(error) << error.message();
        m_moved = true;

        clear_char(&m_builder, MOB_VOID);
        m_builder.player.name = m_builder_name;
        m_builder.player.level = LEVEL_IMMORT;
        m_builder.desc = m_descriptor.get();
        m_descriptor.get()->character = &m_builder;
        m_builder.temp = calloc(1, sizeof(shape_zone));
    }

    void TearDown() override
    {
        if (m_builder.temp != nullptr) {
            free_zone(&m_builder);
        }
        if (m_moved) {
            std::error_code error;
            std::filesystem::current_path(m_previous_folder, error);
        }
    }

    // Writes zone 45 with <zone_name> to world/zon/45.zon.
    void write_zone_named(std::string_view zone_name)
    {
        std::ofstream file(m_folder.path() / "world/zon/45.zon", std::ios::binary);
        file << "#45 " << zone_name << "~\n"
             << "A description.~\n"
             << "~\n"
             << "0\n"
             << "L 3 4 0\n"
             << "4599\n"
             << "10\n"
             << "2\n"
             << "S\n"
             << "$~\n";
    }

    // The scratch folder holding world/zon.
    ScopedTemporaryFolder m_folder;
    // The working folder before SetUp moved into the scratch folder.
    std::filesystem::path m_previous_folder;
    // Whether SetUp moved the working folder, so TearDown moves it back.
    bool m_moved = false;
    // Collects what the builder is shown.
    CapturingDescriptor m_descriptor;
    // Storage for the builder's name.
    char m_builder_name[16] = "Builder";
    // The builder, whose temp holds the zone editor's state.
    char_data m_builder {};
};

TEST_F(ZoneEditorLoadTest, ReportsAnOrdinaryZoneNameAsBefore)
{
    write_zone_named("Lake-town I");

    ASSERT_EQ(load_zone(&m_builder, "zone 45"), 45);

    EXPECT_NE(m_descriptor.output().find(" loading zone #45 Lake-town I\n\r"), std::string::npos)
        << m_descriptor.output();
}

TEST_F(ZoneEditorLoadTest, ReportsAZoneNameLongerThanATypedLineInFull)
{
    const std::string zone_name(TYPED_LINE_LENGTH + 45, 'z');
    write_zone_named(zone_name);

    ASSERT_EQ(load_zone(&m_builder, "zone 45"), 45);

    EXPECT_NE(m_descriptor.output().find(" loading zone #45 " + zone_name + "\n\r"), std::string::npos);
}

/* ---------------------------------------------------------------- world map */

// Draws on the world map and puts the map back as it was afterwards.
class WorldMapSymbolTest : public testing::Test {
protected:
    void SetUp() override
    {
        m_saved_map.assign(world_map, world_map + WORLD_AREA + 1);
    }

    void TearDown() override
    {
        std::copy(m_saved_map.begin(), m_saved_map.end(), world_map);
    }

    // The map cell symbol_to_map draws (x, y) into.
    static int cell_of(int x, int y) { return (y + 1) * (WORLD_SIZE_X + 4) + x * 2 + 1; }

    // Whether the map is byte for byte as SetUp found it.
    bool map_unchanged() const { return std::equal(m_saved_map.begin(), m_saved_map.end(), world_map); }

    // The map as SetUp found it.
    std::vector<char> m_saved_map;
};

TEST_F(WorldMapSymbolTest, DrawsAZoneOnTheMapAsBefore)
{
    symbol_to_map(3, 4, 'Q');

    EXPECT_EQ(world_map[cell_of(3, 4)], 'Q');
}

TEST_F(WorldMapSymbolTest, StillDrawsAZonePastTheRightEdgeInTheLastColumn)
{
    symbol_to_map(WORLD_SIZE_X, 2, 'Q');

    EXPECT_EQ(world_map[cell_of(WORLD_SIZE_X / 2, 2)], 'Q');
}

TEST_F(WorldMapSymbolTest, RefusesAndLogsAZoneBelowTheMap)
{
    testing::internal::CaptureStderr();
    symbol_to_map(3, WORLD_SIZE_Y + 3, 'Q');
    const std::string log_text = testing::internal::GetCapturedStderr();

    EXPECT_TRUE(map_unchanged());
    EXPECT_NE(log_text.find("off the world map"), std::string::npos) << log_text;
}

TEST_F(WorldMapSymbolTest, RefusesAZoneAboveOrLeftOfTheMap)
{
    testing::internal::CaptureStderr();
    symbol_to_map(3, -5, 'Q');
    symbol_to_map(-30, 2, 'Q');
    testing::internal::GetCapturedStderr();

    EXPECT_TRUE(map_unchanged());
}

/* ---------------------------------------------------------------- PK list */

// A PK record from <killer_index> to <victim_index>, made now so it has not expired.
PKILL make_pkill(int killer_index, int victim_index)
{
    PKILL record {};
    record.kill_time = static_cast<int>(std::time(nullptr));
    record.killer = killer_index;
    record.victim = victim_index;
    record.killer_level = 20;
    record.victim_level = 20;
    record.killer_points = 100;
    record.victim_points = -100;
    return record;
}

// The records in the PK file at <path>.
std::vector<PKILL> read_pkill_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    std::vector<PKILL> records(bytes.size() / sizeof(PKILL));
    if (!records.empty()) {
        std::memcpy(records.data(), bytes.data(), records.size() * sizeof(PKILL));
    }
    return records;
}

TEST(PkillFile, WritesARecordBetweenIndexedPlayersWithTheirIdnums)
{
    ScopedTemporaryFolder folder;
    ASSERT_FALSE(folder.path().empty());
    ScopedPlayerIndex index({ "killer", "victim" });
    std::vector<PKILL> records { make_pkill(0, 1) };
    const std::filesystem::path file = folder.path() / "pklist";

    testing::internal::CaptureStderr();
    EXPECT_EQ(pkill_update_file(file.string().c_str(), records.data(), 1), 1);
    testing::internal::GetCapturedStderr();

    const std::vector<PKILL> written = read_pkill_file(file);
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0].killer, 1001);
    EXPECT_EQ(written[0].victim, 1002);
    EXPECT_EQ(written[0].killer_points, 100);
    EXPECT_EQ(written[0].victim_points, -100);
}

TEST(PkillFile, DropsAndLogsARecordNamingAPlayerMissingFromTheIndex)
{
    ScopedTemporaryFolder folder;
    ASSERT_FALSE(folder.path().empty());
    ScopedPlayerIndex index({ "killer", "victim" });
    std::vector<PKILL> records { make_pkill(-1, 1), make_pkill(0, 1), make_pkill(0, -1) };
    const std::filesystem::path file = folder.path() / "pklist";

    testing::internal::CaptureStderr();
    const int written_count = pkill_update_file(file.string().c_str(), records.data(), 3);
    const std::string log_text = testing::internal::GetCapturedStderr();

    EXPECT_EQ(written_count, 1);
    const std::vector<PKILL> written = read_pkill_file(file);
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0].killer, 1001);
    EXPECT_EQ(written[0].victim, 1002);
    EXPECT_NE(log_text.find("SYSERR: pkill_update_file"), std::string::npos) << log_text;
}

/* ---------------------------------------------------------------- legacy player loader */

// The player the loader tests load; the index holds only this name.
constexpr char LOADED_PLAYER_NAME[] = "aragorn";

// Lines every loader test's player text starts with.
constexpr char PLAYER_TEXT_START[] = "#player\n"
                                     "name        aragorn\n"
                                     "title       the Ranger\n"
                                     "host        example.org\n"
                                     "alignment   350\n"
                                     "skills      3 70\n"
                                     "affect      0 5 10 1 2 0\n";

// Loads <player_text> for LOADED_PLAYER_NAME into <record>, returning the loader's result.
int load_text(const std::string& player_text, char_file_u* record)
{
    char name[sizeof(LOADED_PLAYER_NAME)];
    std::memcpy(name, LOADED_PLAYER_NAME, sizeof(name));
    return load_player_from_text(name, player_text.c_str(), record);
}

// A player text: PLAYER_TEXT_START, then <extra_lines>, then the end line.
std::string player_text_with(std::string_view extra_lines)
{
    std::string player_text = PLAYER_TEXT_START;
    player_text.append(extra_lines);
    player_text += "end\n";
    return player_text;
}

TEST(LegacyPlayerLoader, LoadsOrdinaryStringsAndSlotsAsBefore)
{
    ScopedPlayerIndex index({ LOADED_PLAYER_NAME });
    auto record = std::make_unique<char_file_u>();

    ASSERT_EQ(load_text(player_text_with(""), record.get()), 1);

    EXPECT_STREQ(record->name, "aragorn");
    EXPECT_STREQ(record->title, "the Ranger");
    EXPECT_STREQ(record->host, "example.org");
    EXPECT_EQ(record->specials2.alignment, 350);
    EXPECT_EQ(record->skills[3], 70);
    EXPECT_EQ(record->affected[0].type, 5);
    EXPECT_EQ(record->affected[0].duration, 10);
}

TEST(LegacyPlayerLoader, TerminatesATitleThatFillsItsField)
{
    ScopedPlayerIndex index({ LOADED_PLAYER_NAME });
    auto record = std::make_unique<char_file_u>();
    const std::string long_title(85, 'T');

    ASSERT_EQ(load_text(player_text_with("title       " + long_title + "\n"), record.get()), 1);

    ASSERT_LT(strnlen(record->title, sizeof(record->title)), sizeof(record->title));
    EXPECT_EQ(std::string(record->title), long_title.substr(0, sizeof(record->title) - 1));
}

TEST(LegacyPlayerLoader, TerminatesANameLongerThanItsFieldWithoutTouchingThePassword)
{
    ScopedPlayerIndex index({ LOADED_PLAYER_NAME });
    auto record = std::make_unique<char_file_u>();
    const std::string long_name = "abcdefghijklmnopqrst";

    ASSERT_EQ(load_text(player_text_with("password    0123456789\nname        " + long_name + "\n"),
                  record.get()),
        1);

    ASSERT_LT(strnlen(record->name, sizeof(record->name)), sizeof(record->name));
    EXPECT_EQ(std::string(record->name), long_name.substr(0, sizeof(record->name) - 1));

    auto password_only = std::make_unique<char_file_u>();
    ASSERT_EQ(load_text(player_text_with("password    0123456789\n"), password_only.get()), 1);
    EXPECT_EQ(std::memcmp(record->pwd, password_only->pwd, sizeof(record->pwd)), 0);
}

TEST(LegacyPlayerLoader, SkipsAndLogsAnAffectSlotPastTheTable)
{
    ScopedPlayerIndex index({ LOADED_PLAYER_NAME });
    auto expected = std::make_unique<char_file_u>();
    auto loaded = std::make_unique<char_file_u>();
    ASSERT_EQ(load_text(player_text_with(""), expected.get()), 1);

    testing::internal::CaptureStderr();
    const int result = load_text(player_text_with("affect      " + std::to_string(MAX_AFFECT) + " 7 7 7 7 7\n"),
        loaded.get());
    const std::string log_text = testing::internal::GetCapturedStderr();

    ASSERT_EQ(result, 1);
    EXPECT_EQ(std::memcmp(expected.get(), loaded.get(), sizeof(char_file_u)), 0);
    EXPECT_NE(log_text.find("SYSERR: load_player_from_text"), std::string::npos) << log_text;
}

TEST(LegacyPlayerLoader, SkipsAnAffectSlotOutsideTheRecord)
{
    ScopedPlayerIndex index({ LOADED_PLAYER_NAME });
    auto loaded = std::make_unique<char_file_u>();
    // The first slot starting at or past the end of the record.
    const size_t bytes_after_table = sizeof(char_file_u) - offsetof(char_file_u, affected);
    const size_t slot_past_record = (bytes_after_table + sizeof(affected_type) - 1) / sizeof(affected_type);

    testing::internal::CaptureStderr();
    const int result = load_text(player_text_with("affect      " + std::to_string(slot_past_record) + " 7 7 7 7 7\n"
                                     + "affect      -1 7 7 7 7 7\n"),
        loaded.get());
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(result, 1);
}

TEST(LegacyPlayerLoader, SkipsAnArraySlotPastItsField)
{
    ScopedPlayerIndex index({ LOADED_PLAYER_NAME });
    auto expected = std::make_unique<char_file_u>();
    auto loaded = std::make_unique<char_file_u>();
    ASSERT_EQ(load_text(player_text_with(""), expected.get()), 1);

    testing::internal::CaptureStderr();
    const int result = load_text(player_text_with("skills      " + std::to_string(MAX_SKILLS) + " 99\n"
                                     + "talks       -1 99\n"),
        loaded.get());
    testing::internal::GetCapturedStderr();

    ASSERT_EQ(result, 1);
    EXPECT_EQ(std::memcmp(expected.get(), loaded.get(), sizeof(char_file_u)), 0);
}

/* ---------------------------------------------------------------- PK list across boots */

// Saved PK state: the table boot_pkills fills and the working folder it reads misc/pklist from.
// The constructor empties the table and moves into <folder>; the destructor takes the players
// back out of the rankings, frees the table and puts everything back.
class ScopedPkillBoot {
public:
    explicit ScopedPkillBoot(const std::filesystem::path& folder)
        : m_previous_table(pkill_tab)
        , m_previous_length(pkill_tab_len)
    {
        pkill_tab = nullptr;
        pkill_tab_len = 0;
        std::error_code error;
        m_previous_folder = std::filesystem::current_path(error);
        std::filesystem::create_directories(folder / "misc", error);
        std::filesystem::current_path(folder, error);
        m_moved = !error;
        EXPECT_TRUE(m_moved) << error.message();
    }

    ~ScopedPkillBoot()
    {
        forget_boot();
        pkill_tab = m_previous_table;
        pkill_tab_len = m_previous_length;
        if (m_moved) {
            std::error_code error;
            std::filesystem::current_path(m_previous_folder, error);
        }
    }

    ScopedPkillBoot(const ScopedPkillBoot&) = delete;
    ScopedPkillBoot& operator=(const ScopedPkillBoot&) = delete;

    // Takes every indexed player out of the rankings and frees the table a boot made.
    void forget_boot()
    {
        for (int index = 0; index <= top_of_p_table; ++index) {
            pkill_unref_character_by_index(index);
            player_table[index].warpoints = 0;
            player_table[index].rank = PKILL_UNRANKED;
            player_table[index].totalrank = PKILL_UNRANKED;
        }
        free(pkill_tab);
        pkill_tab = nullptr;
        pkill_tab_len = 0;
    }

private:
    // The PK table in place before this object.
    PKILL* m_previous_table;
    // Its length.
    int m_previous_length;
    // The working folder before this object moved.
    std::filesystem::path m_previous_folder;
    // Whether the working folder was moved, so the destructor moves it back.
    bool m_moved = false;
};

// A PK record in file form, between <killer_idnum> and <victim_idnum>, made now.
PKILL make_stored_pkill(int killer_idnum, int victim_idnum, int points)
{
    PKILL record = make_pkill(killer_idnum, victim_idnum);
    record.killer_points = points;
    record.victim_points = -points;
    return record;
}

// Whether two file records hold the same values.
bool same_record(const PKILL& left, const PKILL& right)
{
    return left.kill_time == right.kill_time && left.killer == right.killer && left.victim == right.victim
        && left.killer_level == right.killer_level && left.victim_level == right.victim_level
        && left.killer_points == right.killer_points && left.victim_points == right.victim_points;
}

TEST(PkillBoot, KeepsARecordNamingAMissingPlayerAndItsPointsAcrossBoots)
{
    ScopedTemporaryFolder folder;
    ASSERT_FALSE(folder.path().empty());
    ScopedPlayerIndex index({ "present", "other" }); /* idnums 1001 and 1002 */
    ScopedPkillBoot boot(folder.path());
    const std::vector<PKILL> stored {
        make_stored_pkill(1001, 1002, 100), /* both indexed */
        make_stored_pkill(5555, 1002, 70), /* killer missing */
        make_stored_pkill(1001, 6666, 40), /* victim missing */
    };
    {
        std::ofstream file(folder.path() / "misc/pklist", std::ios::binary);
        file.write(reinterpret_cast<const char*>(stored.data()), stored.size() * sizeof(PKILL));
    }

    for (int boot_number = 1; boot_number <= 2; ++boot_number) {
        testing::internal::CaptureStderr();
        boot_pkills();
        testing::internal::GetCapturedStderr();

        EXPECT_EQ(player_table[0].warpoints, 140) << "boot " << boot_number;
        EXPECT_EQ(player_table[1].warpoints, -170) << "boot " << boot_number;
        const std::vector<PKILL> written = read_pkill_file(folder.path() / "misc/pklist");
        ASSERT_EQ(written.size(), stored.size()) << "boot " << boot_number;
        for (size_t record = 0; record < stored.size(); ++record) {
            EXPECT_TRUE(same_record(written[record], stored[record])) << "boot " << boot_number << ", record " << record;
        }
        boot.forget_boot();
    }
}

/* ---------------------------------------------------------------- fame command */

// A player looking up PK records with the fame command, against a PK table the test fills.
class FameCommandTest : public testing::Test {
protected:
    void SetUp() override
    {
        ensure_room_zero();
        m_previous_table = pkill_tab;
        m_previous_length = pkill_tab_len;
        clear_char(&m_player, MOB_VOID);
        m_player.player.name = m_player_name;
        m_player.player.level = 10;
        m_player.in_room = 0;
        m_player.desc = m_descriptor.get();
        m_descriptor.get()->character = &m_player;
    }

    void TearDown() override
    {
        pkill_tab = m_previous_table;
        pkill_tab_len = m_previous_length;
        free(m_descriptor.get()->showstr_head);
        m_descriptor.get()->showstr_head = nullptr;
        m_descriptor.get()->showstr_point = nullptr;
    }

    // Makes the PK table <count> kills of player index 1 by player index 0, all made now.
    void fill_kills(int count)
    {
        m_kills.assign(count, make_pkill(0, 1));
        pkill_tab = m_kills.data();
        pkill_tab_len = count;
    }

    // Runs fame with <argument>.
    void fame(std::string argument)
    {
        do_fame(&m_player, &argument[0], nullptr, 0, 0);
    }

    // Collects what the player is told.
    CapturingDescriptor m_descriptor;
    // Storage for the player's name.
    char m_player_name[16] = "Looker";
    // The player typing fame.
    char_data m_player {};
    // The PK table while a test runs.
    std::vector<PKILL> m_kills;
    // The PK table before the test.
    PKILL* m_previous_table = nullptr;
    // Its length.
    int m_previous_length = 0;
};

// How many times <needle> occurs in <haystack>.
int count_of(const std::string& haystack, std::string_view needle)
{
    int count = 0;
    for (size_t position = haystack.find(needle); position != std::string::npos;
         position = haystack.find(needle, position + needle.size())) {
        ++count;
    }
    return count;
}

TEST_F(FameCommandTest, ListsEveryRecordOfAPlayerWhoseRecordsOutgrowTheOldBuffer)
{
    ScopedPlayerIndex index({ "aragorn", "per%s%s%sent" });
    constexpr int KILL_COUNT = 150; /* about 10,000 characters of records */
    fill_kills(KILL_COUNT);

    fame("aragorn");

    const std::string output = m_descriptor.output();
    EXPECT_EQ(count_of(output, "Aragorn killed Per%s%s%sent.\r\n"), KILL_COUNT);
    EXPECT_NE(output.find("There were 150 records found about Aragorn, total fame 0.\r\n"), std::string::npos);
}

TEST_F(FameCommandTest, ShowsAPercentSignInANameAsWritten)
{
    ScopedPlayerIndex index({ "aragorn", "per%s%s%sent" });
    fill_kills(1);

    fame("aragorn");

    const std::string output = m_descriptor.output();
    EXPECT_NE(output.find("Aragorn killed Per%s%s%sent.\r\n"), std::string::npos) << output;
    EXPECT_NE(output.find("There was 1 record found about Aragorn, total fame 0.\r\n"), std::string::npos) << output;
}

TEST_F(FameCommandTest, ShowsAPercentSignInTheDaysBattlesAsWritten)
{
    ScopedPlayerIndex index({ "aragorn", "per%s%s%sent" });
    fill_kills(3);

    fame("all");

    EXPECT_EQ(count_of(m_descriptor.output(), "Aragorn killed Per%s%s%sent.\r\n"), 3) << m_descriptor.output();
}

/* ---------------------------------------------------------------- shaping editors, more */

TEST_F(ZoneEditorLoadTest, ListsARowWhoseCommentOutgrowsTheListBuffer)
{
    const std::string comment(MAX_STRING_LENGTH + 800, 'c');
    {
        std::ofstream file(m_folder.path() / "world/zon/45.zon", std::ios::binary);
        file << "#45 Lake-town I~\n"
             << "A description.~\n"
             << "~\n"
             << "0\n"
             << "L 3 4 0\n"
             << "4599\n"
             << "10\n"
             << "2\n"
             << "* " << comment << "\n"
             << "S\n"
             << "$~\n";
    }
    ASSERT_EQ(load_zone(&m_builder, "zone 45"), 45);
    shape_zone* zone = static_cast<shape_zone*>(m_builder.temp);
    zone->mask.command = '*';
    zone->mask.if_flag = zone->mask.arg1 = zone->mask.arg2 = zone->mask.arg3 = -1;
    zone->mask.arg4 = zone->mask.arg5 = zone->mask.arg6 = zone->mask.arg7 = -1;

    list_zone(&m_builder);

    EXPECT_NE(m_descriptor.output().find("Filter is: *"), std::string::npos);
}

TEST(ShapeCommand, RefusesAnUnknownWordLongerThanTheOldBuffer)
{
    CapturingDescriptor descriptor;
    char builder_name[16] = "Builder";
    char_data builder {};
    clear_char(&builder, MOB_VOID);
    builder.player.name = builder_name;
    builder.player.level = LEVEL_IMMORT;
    builder.desc = descriptor.get();
    descriptor.get()->character = &builder;
    std::string argument(200, 'w');

    do_shape(&builder, &argument[0], nullptr, 0, 0);

    EXPECT_NE(descriptor.output().find("Unrecognized parameter."), std::string::npos) << descriptor.output();
}

/* ---------------------------------------------------------------- act */

// A player in room 0 reading what act sends them.
class ActMessageTest : public testing::Test {
protected:
    void SetUp() override
    {
        ensure_room_zero();
        clear_char(&m_player, MOB_VOID);
        m_player.player.name = m_player_name;
        m_player.player.level = 10;
        m_player.in_room = 0;
        m_player.specials.position = POSITION_STANDING; /* act skips sleepers */
        m_player.desc = m_descriptor.get();
        m_descriptor.get()->character = &m_player;
    }

    // What act sends the player for <message> with <text> as $T.
    std::string act_to_player(const char* message, const char* text = nullptr)
    {
        act(message, FALSE, &m_player, nullptr, const_cast<char*>(text), TO_CHAR);
        return m_descriptor.output();
    }

    // Collects what the player is told.
    CapturingDescriptor m_descriptor;
    // Storage for the player's name.
    char m_player_name[16] = "Speaker";
    // The player.
    char_data m_player {};
};

TEST_F(ActMessageTest, SendsAnOrdinaryMessageAsBefore)
{
    EXPECT_EQ(act_to_player("you say '$T'", "hello"), "You say 'hello'\n\r");
}

TEST_F(ActMessageTest, CutsAMessageLongerThanItsBufferAndStillEndsTheLine)
{
    const std::string text(MAX_STRING_LENGTH + 500, 'X');

    const std::string output = act_to_player("$T", text.c_str());

    ASSERT_EQ(output.size(), static_cast<size_t>(MAX_STRING_LENGTH - 1));
    EXPECT_EQ(output.substr(output.size() - 2), "\n\r");
    EXPECT_EQ(output.find_first_not_of('X'), output.size() - 2);
}

TEST_F(ActMessageTest, DropsADollarSignEndingTheMessage)
{
    testing::internal::CaptureStderr();
    const std::string output = act_to_player("that costs 5$");
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(output, "That costs 5\n\r");
}

TEST_F(ActMessageTest, DropsAnUnknownCodeBeforeAnyOther)
{
    testing::internal::CaptureStderr();
    const std::string output = act_to_player("a$zb");
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(output, "Ab\n\r");
}

/* ---------------------------------------------------------------- typed text in act formats */

// A speaker and a listener standing in room 0. Typed text reaches commands with every '$'
// doubled by process_input, so the tests pass it that way.
class TypedTextChannelTest : public testing::Test {
protected:
    void SetUp() override
    {
        ensure_room_zero();
        m_saved_people = world[0].people;
        set_up(m_speaker, m_speaker_name, m_speaker_descriptor);
        set_up(m_listener, m_listener_name, m_listener_descriptor);
        SET_BIT(PRF_FLAGS(&m_speaker), PRF_ECHO);
        m_speaker.next_in_room = &m_listener;
        m_listener.next_in_room = nullptr;
        world[0].people = &m_speaker;
    }

    void TearDown() override { world[0].people = m_saved_people; }

    // Makes <person> a standing player called <name> in room 0, reading through <descriptor>.
    static void set_up(char_data& person, char* name, CapturingDescriptor& descriptor)
    {
        clear_char(&person, MOB_VOID);
        person.player.name = name;
        person.player.level = 10;
        person.in_room = 0;
        person.specials.position = POSITION_STANDING;
        person.desc = descriptor.get();
        descriptor.get()->character = &person;
        descriptor.get()->descriptor = 1; /* say speaks only to connected listeners */
    }

    // Collects what the speaker is told.
    CapturingDescriptor m_speaker_descriptor;
    // Collects what the listener hears.
    CapturingDescriptor m_listener_descriptor;
    // Storage for the speaker's name.
    char m_speaker_name[16] = "Speaker";
    // Storage for the listener's name.
    char m_listener_name[16] = "Listener";
    // The player typing.
    char_data m_speaker {};
    // The player hearing.
    char_data m_listener {};
    // Room 0's people before the test.
    char_data* m_saved_people = nullptr;
};

TEST_F(TypedTextChannelTest, SayShowsTypedCodesLiterally)
{
    char typed[] = "$$o $$T $$F $$p $$n hello";

    do_say(&m_speaker, typed, nullptr, 0, 0);

    EXPECT_EQ(m_listener_descriptor.output(), "Speaker says '$o $T $F $p $n hello'\n\r");
    EXPECT_EQ(m_speaker_descriptor.output(), "You say '$o $T $F $p $n hello'\n\r");
}

TEST_F(TypedTextChannelTest, SayShowsOrdinaryTextAsBefore)
{
    char typed[] = "well met";

    do_say(&m_speaker, typed, nullptr, 0, 0);

    EXPECT_EQ(m_listener_descriptor.output(), "Speaker says 'well met'\n\r");
}

TEST_F(TypedTextChannelTest, EmoteShowsTypedCodesLiterally)
{
    char typed[] = "grins $$o $$T $$F $$p";

    do_emote(&m_speaker, typed, nullptr, 0, 0);

    EXPECT_EQ(m_listener_descriptor.output(), "Speaker grins $o $T $F $p\n\r");
    EXPECT_EQ(m_speaker_descriptor.output(), "Speaker grins $o $T $F $p\n\r");
}

/* ---------------------------------------------------------------- pager */

TEST(Pager, ShowsAPageLongerThanItsBufferInParts)
{
    CapturingDescriptor descriptor;
    std::string text(MAX_STRING_LENGTH + 2000, 'p');
    text += "\n\r";

    page_string(descriptor.get(), &text[0], 1);

    EXPECT_EQ(descriptor.output(), std::string(MAX_STRING_LENGTH - 2, 'p'));
    ASSERT_NE(descriptor.get()->showstr_point, nullptr) << "the rest waits for the next page";
    EXPECT_EQ(std::string(descriptor.get()->showstr_point), std::string(2002, 'p') + "\n\r");
    free(descriptor.get()->showstr_head);
}

TEST(Pager, ShowsAShortPageAsBefore)
{
    CapturingDescriptor descriptor;
    char text[] = "first line\n\rsecond line\n\r";

    page_string(descriptor.get(), text, 1);

    EXPECT_EQ(descriptor.output(), "first line\n\rsecond line\n\r");
    EXPECT_EQ(descriptor.get()->showstr_point, nullptr);
}

/* ---------------------------------------------------------------- editor file names */

// A builder in the room or object editor, whose editor state is a member.
class EditorFileNameTest : public testing::Test {
protected:
    void SetUp() override
    {
        clear_char(&m_builder, MOB_VOID);
        m_builder.player.name = m_builder_name;
        m_builder.player.level = LEVEL_IMMORT;
        m_builder.desc = m_descriptor.get();
        m_descriptor.get()->character = &m_builder;
    }

    void TearDown() override { m_builder.temp = nullptr; }

    // A command naming a file far too long for the editors' 80-byte file fields.
    static std::string long_name_command(std::string_view command)
    {
        std::string text(command);
        text += " 12";
        text.append(100, 'x');
        return text;
    }

    // Collects what the builder is shown.
    CapturingDescriptor m_descriptor;
    // Storage for the builder's name.
    char m_builder_name[16] = "Builder";
    // The builder.
    char_data m_builder {};
    // Room editor state, when a test uses the room editor.
    shape_room m_room_editor {};
    // Object editor state, when a test uses the object editor.
    shape_object m_object_editor {};
};

TEST_F(EditorFileNameTest, RoomCreateRefusesAFileNameTooLongToStore)
{
    m_builder.temp = &m_room_editor;
    const std::string command = long_name_command("create");

    EXPECT_EQ(create_room(&m_builder, command.c_str()), -1);

    EXPECT_NE(m_descriptor.output().find("That file name is too long."), std::string::npos) << m_descriptor.output();
}

TEST_F(EditorFileNameTest, RoomAddReadsTheFileNameTyped)
{
    m_builder.temp = &m_room_editor;

    EXPECT_EQ(append_room(&m_builder, "add 45"), -1); /* no permission in the test */

    EXPECT_STREQ(m_room_editor.f_from, "world/wld/45.wld");
    EXPECT_STREQ(m_room_editor.f_old, "world/wld/oldroms/45.wld");
}

TEST_F(EditorFileNameTest, RoomAddRefusesAFileNameTooLongToStore)
{
    m_builder.temp = &m_room_editor;
    const std::string command = long_name_command("add");

    EXPECT_EQ(append_room(&m_builder, command.c_str()), -1);

    EXPECT_NE(m_descriptor.output().find("That file name is too long."), std::string::npos) << m_descriptor.output();
    EXPECT_STREQ(m_room_editor.f_from, "");
}

TEST_F(EditorFileNameTest, ObjectAddStoresAFileNameThatFitsAsBefore)
{
    m_builder.temp = &m_object_editor;
    m_object_editor.permission = 1;
    m_object_editor.number = -1;
    char command[] = "add 45";

    EXPECT_EQ(append_object(&m_builder, command), -1); /* no object loaded in the test */

    EXPECT_STREQ(m_object_editor.f_from, "world/mob/45.mob");
}

TEST_F(EditorFileNameTest, ObjectAddRefusesAFileNameTooLongToStore)
{
    m_builder.temp = &m_object_editor;
    m_object_editor.permission = 1;
    m_object_editor.number = -1;
    std::string command = long_name_command("add");

    EXPECT_EQ(append_object(&m_builder, &command[0]), -1);

    EXPECT_NE(m_descriptor.output().find("That file name is too long."), std::string::npos) << m_descriptor.output();
    EXPECT_STREQ(m_object_editor.f_from, "");
}

} // namespace
