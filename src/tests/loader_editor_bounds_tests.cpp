#include "../character_json.h"
#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../pkill.h"
#include "../protos.h"
#include "../structs.h"
#include "../utils.h"
#include "test_cleanup.h"

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
extern char world_map[];

int Crash_report_unrentables(struct char_data* character, struct char_data* receptionist,
    const struct obj_data* object);
void list_simple_proto(const struct char_data* character, const struct char_data* mob);
void list_proto(const struct char_data* character, const struct char_data* mob);
int load_zone(struct char_data* character, const char* argument);
void free_zone(struct char_data* character);
void symbol_to_map(int column, int row, int symbol);
int pkill_update_file(const char* file_name, PKILL pkills[], int pkill_count);
void list_zone(const struct char_data* character);
ACMD(do_fame);
ACMD(do_say);
int create_room(struct char_data* character, const char* argument);
int append_room(struct char_data* character, const char* argument);
int append_object(struct char_data* character, char* argument);
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

    ~CapturingDescriptor() { release_test_descriptor(&m_descriptor); }

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

        constexpr int NO_PROTOTYPE = -1; /* the item number of an object with no prototype */
        clear_object(&m_item);
        m_item.item_number = NO_PROTOTYPE; /* never rentable */
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
    // Frees what clear_char gave m_player when the test ends.
    ClearedCharacterRelease m_player_release { &m_player };
    // The innkeeper refusing.
    char_data m_innkeeper {};
    // Frees what clear_char gave m_innkeeper when the test ends.
    ClearedCharacterRelease m_innkeeper_release { &m_innkeeper };
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
    constexpr size_t LONG_NAME_LENGTH = 200; /* past the 128-byte buffer the refusal was built in */
    const std::string long_name(LONG_NAME_LENGTH, 'k');

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
    // Frees what clear_char gave m_builder when the test ends.
    ClearedCharacterRelease m_builder_release { &m_builder };
    // The prototype being listed.
    char_data m_mob {};
    // Frees what clear_char gave m_mob when the test ends.
    ClearedCharacterRelease m_mob_release { &m_mob };
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
    // Frees what clear_char gave m_builder when the test ends.
    ClearedCharacterRelease m_builder_release { &m_builder };
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

    // The map cell symbol_to_map draws (<column>, <row>) into, laid out as draw_map lays the map
    // out: rows start below the top border, and columns after the left border.
    static int cell_of(int column, int row)
    {
        return (row + MAP_BORDER_WIDTH) * MAP_ROW_LENGTH + column * MAP_CELL_WIDTH
            + MAP_BORDER_WIDTH;
    }

    // One map row: the left border, WORLD_SIZE_X characters, the right border and "\n\r".
    static constexpr int MAP_ROW_LENGTH = WORLD_SIZE_X + 4;
    // The border rows above and below the map, and the border columns left and right of it.
    static constexpr int MAP_BORDER_WIDTH = 1;
    // The characters each map column takes; a zone's symbol is the first.
    static constexpr int MAP_CELL_WIDTH = 2;

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

// The idnum ScopedPlayerIndex gives its first player.
constexpr int FIRST_PLAYER_IDNUM = 1001;
// The idnum ScopedPlayerIndex gives its second player.
constexpr int SECOND_PLAYER_IDNUM = 1002;
// The points make_pkill gives a record's killer; its victim loses as many.
constexpr int MADE_RECORD_POINTS = 100;
// The player index a record names when its player is not in the index.
constexpr int MISSING_PLAYER_INDEX = -1;

TEST(PkillFile, WritesARecordBetweenIndexedPlayersWithTheirIdnums)
{
    ScopedTemporaryFolder folder;
    ASSERT_FALSE(folder.path().empty());
    ScopedPlayerIndex index({ "killer", "victim" });
    std::vector<PKILL> records { make_pkill(0, 1) };
    const std::filesystem::path file = folder.path() / "pklist";

    testing::internal::CaptureStderr();
    const int record_count = static_cast<int>(records.size());
    EXPECT_EQ(pkill_update_file(file.string().c_str(), records.data(), record_count), 1);
    testing::internal::GetCapturedStderr();

    const std::vector<PKILL> written = read_pkill_file(file);
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0].killer, FIRST_PLAYER_IDNUM);
    EXPECT_EQ(written[0].victim, SECOND_PLAYER_IDNUM);
    EXPECT_EQ(written[0].killer_points, MADE_RECORD_POINTS);
    EXPECT_EQ(written[0].victim_points, -MADE_RECORD_POINTS);
}

TEST(PkillFile, DropsAndLogsARecordNamingAPlayerMissingFromTheIndex)
{
    ScopedTemporaryFolder folder;
    ASSERT_FALSE(folder.path().empty());
    ScopedPlayerIndex index({ "killer", "victim" });
    std::vector<PKILL> records { make_pkill(MISSING_PLAYER_INDEX, 1), make_pkill(0, 1),
        make_pkill(0, MISSING_PLAYER_INDEX) };
    const std::filesystem::path file = folder.path() / "pklist";

    testing::internal::CaptureStderr();
    const int record_count = static_cast<int>(records.size());
    const int written_count
        = pkill_update_file(file.string().c_str(), records.data(), record_count);
    const std::string log_text = testing::internal::GetCapturedStderr();

    EXPECT_EQ(written_count, 1);
    const std::vector<PKILL> written = read_pkill_file(file);
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0].killer, FIRST_PLAYER_IDNUM);
    EXPECT_EQ(written[0].victim, SECOND_PLAYER_IDNUM);
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
    const std::string long_title(sizeof(record->title) + 5, 'T'); /* five letters too many */

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
    constexpr int MISSING_KILLER_IDNUM = 5555;
    constexpr int MISSING_VICTIM_IDNUM = 6666;
    constexpr int POINTS_BOTH_INDEXED = 100;
    constexpr int POINTS_KILLER_MISSING = 70;
    constexpr int POINTS_VICTIM_MISSING = 40;
    ScopedPlayerIndex index({ "present", "other" });
    ScopedPkillBoot boot(folder.path());
    const std::vector<PKILL> stored {
        make_stored_pkill(FIRST_PLAYER_IDNUM, SECOND_PLAYER_IDNUM, POINTS_BOTH_INDEXED),
        make_stored_pkill(MISSING_KILLER_IDNUM, SECOND_PLAYER_IDNUM, POINTS_KILLER_MISSING),
        make_stored_pkill(FIRST_PLAYER_IDNUM, MISSING_VICTIM_IDNUM, POINTS_VICTIM_MISSING),
    };
    {
        std::ofstream file(folder.path() / "misc/pklist", std::ios::binary);
        file.write(reinterpret_cast<const char*>(stored.data()), stored.size() * sizeof(PKILL));
    }

    for (int boot_number = 1; boot_number <= 2; ++boot_number) {
        testing::internal::CaptureStderr();
        boot_pkills();
        testing::internal::GetCapturedStderr();

        EXPECT_EQ(player_table[0].warpoints, POINTS_BOTH_INDEXED + POINTS_VICTIM_MISSING)
            << "boot " << boot_number;
        EXPECT_EQ(player_table[1].warpoints, -(POINTS_BOTH_INDEXED + POINTS_KILLER_MISSING))
            << "boot " << boot_number;
        const std::vector<PKILL> written = read_pkill_file(folder.path() / "misc/pklist");
        ASSERT_EQ(written.size(), stored.size()) << "boot " << boot_number;
        for (size_t record = 0; record < stored.size(); ++record) {
            EXPECT_TRUE(same_record(written[record], stored[record])) << "boot " << boot_number << ", record " << record;
        }
        boot.forget_boot();
    }
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
    constexpr int ANY_VALUE = -1; /* the filter value list_zone matches every row's value with */
    zone->mask.command = '*';
    zone->mask.if_flag = zone->mask.arg1 = zone->mask.arg2 = zone->mask.arg3 = ANY_VALUE;
    zone->mask.arg4 = zone->mask.arg5 = zone->mask.arg6 = zone->mask.arg7 = ANY_VALUE;

    list_zone(&m_builder);

    EXPECT_NE(m_descriptor.output().find("Filter is: *"), std::string::npos);
}

TEST(ShapeCommand, RefusesAnUnknownWordLongerThanTheOldBuffer)
{
    CapturingDescriptor descriptor;
    char builder_name[16] = "Builder";
    char_data builder {};
    clear_char(&builder, MOB_VOID);
    ClearedCharacterRelease builder_release(&builder);
    builder.player.name = builder_name;
    builder.player.level = LEVEL_IMMORT;
    builder.desc = descriptor.get();
    descriptor.get()->character = &builder;
    constexpr size_t LONG_WORD_LENGTH = 200; /* past the 50-byte buffer do_shape read words into */
    std::string argument(LONG_WORD_LENGTH, 'w');

    do_shape(&builder, &argument[0], nullptr, 0, 0);

    EXPECT_NE(descriptor.output().find("Unrecognized parameter."), std::string::npos) << descriptor.output();
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

    // A command naming a file far too long for the editors' file fields: the name alone fills
    // f_from, before its folder and extension are added.
    static std::string long_name_command(std::string_view command)
    {
        std::string text(command);
        text += " 12";
        text.append(sizeof(shape_room::f_from), 'x');
        return text;
    }

    // Collects what the builder is shown.
    CapturingDescriptor m_descriptor;
    // Storage for the builder's name.
    char m_builder_name[16] = "Builder";
    // The builder.
    char_data m_builder {};
    // Frees what clear_char gave m_builder when the test ends.
    ClearedCharacterRelease m_builder_release { &m_builder };
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

/* ---------------------------------------------------------------- affect types at load */

// An affect type below the skill table every affect reader indexes.
constexpr int NEGATIVE_AFFECT_TYPE = -4;
// An affect type past the end of that table.
constexpr int AFFECT_TYPE_PAST_THE_TABLE = MAX_SKILLS + 10;

TEST(LegacyPlayerLoader, SkipsAndLogsAnAffectTypeOutsideTheSkillTable)
{
    ScopedPlayerIndex index({ LOADED_PLAYER_NAME });
    auto expected = std::make_unique<char_file_u>();
    auto loaded = std::make_unique<char_file_u>();
    ASSERT_EQ(load_text(player_text_with(""), expected.get()), 1);

    testing::internal::CaptureStderr();
    const std::string affect_lines = "affect      2 " + std::to_string(MAX_SKILLS) + " 10 1 2 0\n"
        + "affect      3 " + std::to_string(NEGATIVE_AFFECT_TYPE) + " 10 1 2 0\n";
    const int result = load_text(player_text_with(affect_lines), loaded.get());
    const std::string log_text = testing::internal::GetCapturedStderr();

    ASSERT_EQ(result, 1);
    EXPECT_EQ(std::memcmp(expected.get(), loaded.get(), sizeof(char_file_u)), 0);
    EXPECT_NE(log_text.find("has type " + std::to_string(MAX_SKILLS)), std::string::npos) << log_text;
    EXPECT_NE(log_text.find("has type " + std::to_string(NEGATIVE_AFFECT_TYPE)), std::string::npos)
        << log_text;
}

TEST(CharacterJsonLoader, SkipsAndLogsAnAffectTypeOutsideTheSkillTable)
{
    auto source = std::make_unique<char_file_u>();
    std::snprintf(source->name, sizeof(source->name), "%s", "aragorn");
    constexpr int KEPT_AFFECT_TYPE = 5;
    constexpr int AFFECT_DURATION = 10;
    source->affected[0].type = KEPT_AFFECT_TYPE;
    source->affected[0].duration = AFFECT_DURATION;
    source->affected[1].type = AFFECT_TYPE_PAST_THE_TABLE;
    source->affected[1].duration = AFFECT_DURATION;
    source->affected[1].bitvector = AFF_POISON;
    source->affected[2].type = NEGATIVE_AFFECT_TYPE;
    source->affected[2].duration = AFFECT_DURATION;
    const character_json::CharacterData character = character_json::character_data_from_store(*source);
    auto stored = std::make_unique<char_file_u>();
    std::string error_message;

    testing::internal::CaptureStderr();
    const bool applied = character_json::apply_character_data_to_store(character, stored.get(), &error_message);
    const std::string log_text = testing::internal::GetCapturedStderr();

    ASSERT_TRUE(applied) << error_message;
    EXPECT_EQ(stored->affected[0].type, KEPT_AFFECT_TYPE);
    EXPECT_EQ(stored->affected[0].duration, AFFECT_DURATION);
    EXPECT_EQ(stored->affected[1].type, 0);
    EXPECT_EQ(stored->affected[1].bitvector, 0);
    EXPECT_EQ(stored->affected[2].type, 0);
    EXPECT_NE(log_text.find("has type " + std::to_string(AFFECT_TYPE_PAST_THE_TABLE)),
        std::string::npos)
        << log_text;
    EXPECT_NE(log_text.find("has type " + std::to_string(NEGATIVE_AFFECT_TYPE)), std::string::npos)
        << log_text;
}

/* ---------------------------------------------------------------- pager */

/* How far past the pager's buffer the long page's line runs. */
constexpr size_t PAGE_OVERRUN = 2000;
/* The most of a page show_string sends at once: its MAX_STRING_LENGTH buffer, less room for a line
 * end's '\r' and the terminator. */
constexpr size_t PAGE_PART_LENGTH = MAX_STRING_LENGTH - 2;

TEST(Pager, ShowsAPageLongerThanItsBufferInParts)
{
    CapturingDescriptor descriptor;
    const size_t line_length = MAX_STRING_LENGTH + PAGE_OVERRUN;
    std::string text(line_length, 'p');
    text += "\n\r";

    page_string(descriptor.get(), &text[0], TRUE);

    EXPECT_EQ(descriptor.output(), std::string(PAGE_PART_LENGTH, 'p'));
    ASSERT_NE(descriptor.get()->showstr_point, nullptr) << "the rest waits for the next page";
    EXPECT_EQ(std::string(descriptor.get()->showstr_point),
        std::string(line_length - PAGE_PART_LENGTH, 'p') + "\n\r");
    free(descriptor.get()->showstr_head);
}

TEST(Pager, ShowsAShortPageAsBefore)
{
    CapturingDescriptor descriptor;
    char text[] = "first line\n\rsecond line\n\r";

    page_string(descriptor.get(), text, TRUE);

    EXPECT_EQ(descriptor.output(), "first line\n\rsecond line\n\r");
    EXPECT_EQ(descriptor.get()->showstr_point, nullptr);
}
} // namespace
