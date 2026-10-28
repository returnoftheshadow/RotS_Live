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

} // namespace
