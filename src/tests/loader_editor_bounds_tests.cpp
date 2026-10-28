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
    // Frees what clear_char gave m_player when the test ends.
    ClearedCharacterRelease m_player_release { &m_player };
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
    const std::string summary = "There were " + std::to_string(KILL_COUNT)
        + " records found about Aragorn, total fame 0.\r\n";
    EXPECT_NE(output.find(summary), std::string::npos);
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
    constexpr int KILL_COUNT = 3;
    fill_kills(KILL_COUNT);

    fame("all");

    EXPECT_EQ(count_of(m_descriptor.output(), "Aragorn killed Per%s%s%sent.\r\n"), KILL_COUNT)
        << m_descriptor.output();
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
    // Frees what clear_char gave m_player when the test ends.
    ClearedCharacterRelease m_player_release { &m_player };
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
    // Frees what clear_char gave m_speaker when the test ends.
    ClearedCharacterRelease m_speaker_release { &m_speaker };
    // The player hearing.
    char_data m_listener {};
    // Frees what clear_char gave m_listener when the test ends.
    ClearedCharacterRelease m_listener_release { &m_listener };
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
} // namespace
