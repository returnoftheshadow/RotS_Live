// Divination: the report a mystic gets of the room they stand in.

#include "../spells.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <deque>
#include <string>
#include <string_view>

extern struct room_data world;
extern char* dirs[];
extern char* room_bits[];
extern char* sector_types[];
void clear_object(struct obj_data* obj);

namespace {

constexpr int DIVINATION_ROOM = 973;
constexpr int HALL_ROOM = 974;
constexpr char LONG_NAME[] = "a long heavy two-handed sword of the north";

size_t count_occurrences(std::string_view text, std::string_view needle)
{
    size_t count = 0;
    for (size_t found_at = text.find(needle); found_at != std::string_view::npos;
         found_at = text.find(needle, found_at + needle.size())) {
        ++count;
    }
    return count;
}

// The line of <report> that starts with <header>, up to and including its "\n\r".
std::string report_line(std::string_view report, std::string_view header)
{
    const size_t start = report.find(header);
    if (start == std::string_view::npos) {
        return "";
    }
    const size_t end = report.find("\n\r", start + header.size());
    if (end == std::string_view::npos) {
        return std::string(report.substr(start));
    }
    return std::string(report.substr(start, end + 2 - start));
}

// A mystic standing in a lit DIVINATION_ROOM, with a door north to HALL_ROOM. Tests add the people
// and objects the room holds; the mystic's output is readable.
class DivinationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        for (int index = 0; index < ROOM_COUNT; ++index) {
            m_saved_rooms[index] = world[DIVINATION_ROOM + index];
        }

        room_data& room = world[DIVINATION_ROOM];
        room.number = 99000 + DIVINATION_ROOM;
        room.sector_type = SECT_FIELD;
        room.room_flags = INDOORS;
        room.light = 1;
        room.people = nullptr;
        room.contents = nullptr;
        for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
            room.dir_option[direction] = nullptr;
        }
        m_north_door.keyword = m_door_keyword;
        m_north_door.exit_info = EX_ISDOOR | EX_CLOSED;
        m_north_door.key = -1;
        m_north_door.to_room = HALL_ROOM;
        room.dir_option[NORTH] = &m_north_door;
        world[HALL_ROOM].number = 99000 + HALL_ROOM;
        world[HALL_ROOM].name = m_hall_name;

        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        m_mystic.player.name = m_mystic_name;
        m_mystic.player.level = 30;
        m_mystic.desc = &m_descriptor;
        m_descriptor.character = &m_mystic;
        stand_in_room(&m_mystic);
    }

    void TearDown() override
    {
        if (m_descriptor.large_outbuf != nullptr) {
            free(m_descriptor.large_outbuf->text);
            free(m_descriptor.large_outbuf);
        }
        for (int index = 0; index < ROOM_COUNT; ++index) {
            world[DIVINATION_ROOM + index] = m_saved_rooms[index];
        }
    }

    // Puts <character> at the head of the room's people, as char_to_room does.
    static void stand_in_room(char_data* character)
    {
        character->in_room = DIVINATION_ROOM;
        character->next_in_room = world[DIVINATION_ROOM].people;
        world[DIVINATION_ROOM].people = character;
    }

    // Adds <count> mobs called <short_description> to the room.
    void add_mobs(size_t count, const char* short_description)
    {
        for (size_t added = 0; added < count; ++added) {
            char_data& mob = m_mobs.emplace_back();
            mob.specials2.act = MOB_ISNPC;
            mob.player.short_descr = const_cast<char*>(short_description);
            stand_in_room(&mob);
        }
    }

    // Puts <count> objects called <short_description> on the room's floor, at the head of its
    // contents, as obj_to_room does.
    void add_objects(size_t count, const char* short_description)
    {
        for (size_t added = 0; added < count; ++added) {
            obj_data& object = m_objects.emplace_back();
            clear_object(&object);
            object.short_description = const_cast<char*>(short_description);
            object.in_room = DIVINATION_ROOM;
            object.next_content = world[DIVINATION_ROOM].contents;
            world[DIVINATION_ROOM].contents = &object;
        }
    }

    std::string divine()
    {
        spell_divination(&m_mystic, nullptr, 0, nullptr, nullptr, 0, 0);
        return m_descriptor.output;
    }

    // DIVINATION_ROOM and HALL_ROOM.
    static constexpr int ROOM_COUNT = 2;

    // The casting mystic, a PC.
    char_data m_mystic {};
    // Mobs add_mobs stood in the room; a deque, so adding more never moves them.
    std::deque<char_data> m_mobs;
    // Objects add_objects put on the floor; a deque, so adding more never moves them.
    std::deque<obj_data> m_objects;
    // The closed door north, owned here.
    room_direction_data m_north_door {};
    // The mystic's connection; its output holds the report.
    descriptor_data m_descriptor {};
    // The rooms from before the test, put back afterwards.
    room_data m_saved_rooms[ROOM_COUNT] {};
    // The mystic's name.
    char m_mystic_name[8] = "Seer";
    // The door's keyword.
    char m_door_keyword[8] = "gate";
    // The name of the room beyond the door.
    char m_hall_name[16] = "A Quiet Hall";
};

TEST_F(DivinationTest, ReportsASmallRoomInFull)
{
    add_mobs(1, "a grey wolf");
    add_objects(1, "a shield");
    add_objects(1, "a sword");
    char flags[256];
    sprintbit(INDOORS, room_bits, flags, 0);

    const std::string report = divine();

    const std::string expected = std::string("You feel confident about your location.\n\r (#99973) [ ")
        + sector_types[SECT_FIELD] + ", " + flags + "], Exits are:\n\r"
        + "north: to A Quiet Hall (#99974)\n\r"
        + "     door 'gate', key 'None'.\n\r"
        + "Living beings in the room:\n\ra grey wolf, Seer.\n\r"
        + "Objects in the room:\n\ra sword, a shield.\n\r";
    EXPECT_EQ(report, expected);
}

TEST_F(DivinationTest, ReportsAnEmptyFloorAsNone)
{
    const std::string report = divine();

    EXPECT_NE(report.find("Living beings in the room:\n\rSeer.\n\r"), std::string::npos) << report;
    EXPECT_NE(report.find("Objects in the room:\n\r None.\n\r"), std::string::npos) << report;
}

TEST_F(DivinationTest, ALitteredFloorListsWhatFitsAndSaysThereIsMore)
{
    add_objects(60, LONG_NAME);

    const std::string objects = report_line(divine(), "Objects in the room:\n\r");

    EXPECT_LE(objects.size(), DIVINATION_LIST_LIMIT);
    EXPECT_EQ(objects.size() - objects.rfind(", and more.\n\r"), std::string(", and more.\n\r").size())
        << objects;
    // Each name and its ", " is 44 characters: 22 for the header and 11 for the ending leave room
    // for the whole names that fit.
    const size_t entry_length = std::string(LONG_NAME).size() + 2;
    const size_t names_that_fit = (DIVINATION_LIST_LIMIT - 22 - 11) / entry_length;
    EXPECT_EQ(count_occurrences(objects, LONG_NAME), names_that_fit) << objects;
}

TEST_F(DivinationTest, ACrowdedRoomListsWhoFitsAndSaysThereIsMore)
{
    add_mobs(40, LONG_NAME);

    const std::string people = report_line(divine(), "Living beings in the room:\n\r");

    EXPECT_LE(people.size(), DIVINATION_LIST_LIMIT);
    EXPECT_NE(people.find(", and more.\n\r"), std::string::npos) << people;
    EXPECT_EQ(people.find("Seer"), std::string::npos) << "the mystic, first in, is listed last";
}

TEST_F(DivinationTest, AFloorThatJustFitsIsListedInFull)
{
    // The header is 22 characters and each entry 44, the last one 45 as it ends ".\n\r".
    const size_t entry_length = std::string(LONG_NAME).size() + 2;
    const size_t names_that_fit = (DIVINATION_LIST_LIMIT - 22 - 1) / entry_length;
    add_objects(names_that_fit, LONG_NAME);

    const std::string objects = report_line(divine(), "Objects in the room:\n\r");

    EXPECT_EQ(count_occurrences(objects, LONG_NAME), names_that_fit) << objects;
    EXPECT_EQ(objects.find("and more"), std::string::npos) << objects;
}

} // namespace
