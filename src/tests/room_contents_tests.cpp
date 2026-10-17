// A room floor holding a thousand objects is legitimate -- a mass quit drops every quitter's gear in
// the room they quit in. obj_to_room's cycle guard treated it as corruption.

#include "../handler.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

extern struct room_data world;
extern int top_of_world;
extern struct descriptor_data* descriptor_list;
void clear_object(struct obj_data* obj);
void clear_char(struct char_data* ch, int mode);
void list_obj_to_char(struct obj_data* list, struct char_data* ch, int mode, bool show);

namespace {

// Room 0 with a floor of `count` objects, appended the way extract_char drops a quitting player's
// inventory: straight onto the list, with no obj_to_room call and so no count check.
class FloorFixture {
public:
    explicit FloorFixture(size_t count)
        : m_floor(count)
    {
        if (room_data::BASE_WORLD == nullptr)
            world.create_bulk(1);
        m_saved_top_of_world = top_of_world;
        m_saved_descriptor_list = descriptor_list;
        m_saved_contents = world[0].contents;
        descriptor_list = nullptr; // obj_to_room mudlogs; no stale descriptors from earlier tests
        top_of_world = 0;

        world[0].contents = nullptr;
        for (size_t index = m_floor.size(); index-- > 0;) {
            clear_object(&m_floor[index]);
            m_floor[index].in_room = 0;
            m_floor[index].next_content = world[0].contents;
            world[0].contents = &m_floor[index];
        }
    }

    ~FloorFixture()
    {
        world[0].contents = m_saved_contents;
        top_of_world = m_saved_top_of_world;
        descriptor_list = m_saved_descriptor_list;
    }

    static size_t listed()
    {
        size_t count = 0;
        for (obj_data* object = world[0].contents; object && count < 100000; object = object->next_content)
            ++count;
        return count;
    }

private:
    std::vector<obj_data> m_floor;
    int m_saved_top_of_world = 0;
    descriptor_data* m_saved_descriptor_list = nullptr;
    obj_data* m_saved_contents = nullptr;
};

// mudlog(..., file = TRUE) writes to stderr.
class ScopedStderrCapture {
public:
    ScopedStderrCapture()
    {
        char path_template[] = "/tmp/rots-room-contents-stderr-XXXXXX";
        m_fd = mkstemp(path_template);
        EXPECT_GE(m_fd, 0);
        m_path = path_template;
        std::fflush(stderr);
        m_saved = dup(STDERR_FILENO);
        dup2(m_fd, STDERR_FILENO);
    }

    std::string finish()
    {
        if (m_saved < 0)
            return m_contents;
        std::fflush(stderr);
        dup2(m_saved, STDERR_FILENO);
        close(m_saved);
        m_saved = -1;
        FILE* file = std::fopen(m_path.c_str(), "rb");
        if (file != nullptr) {
            char buffer[4096];
            size_t count;
            while ((count = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
                m_contents.append(buffer, count);
            std::fclose(file);
        }
        close(m_fd);
        unlink(m_path.c_str());
        return m_contents;
    }

    ~ScopedStderrCapture() { finish(); }

private:
    int m_fd = -1;
    int m_saved = -1;
    std::string m_path;
    std::string m_contents;
};

size_t count_occurrences(const std::string& text, const std::string& needle)
{
    size_t count = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size()))
        ++count;
    return count;
}

TEST(RoomContents, AddingAnObjectToAFloorOfAThousandKeepsEveryObjectListed)
{
    FloorFixture fixture(1000);

    // One more object reaches the floor through obj_to_room -- a quitter's worn gear goes this way.
    obj_data dropped;
    clear_object(&dropped);
    obj_to_room(&dropped, 0);

    EXPECT_EQ(FloorFixture::listed(), 1001u)
        << "objects that still claim this room fell off its contents list; extracting one later "
           "(decay in point_update) walks the list, never finds it, and dereferences NULL in obj_from_room";
}

TEST(RoomContents, AFloorReportsReachingAThousandObjectsOnlyOnce)
{
    // A mass quit sends every quitter's worn gear through obj_to_room onto one floor. Reporting on
    // every one of those drops floods the log and every online god.
    FloorFixture fixture(999);
    obj_data drops[3];
    for (obj_data& drop : drops)
        clear_object(&drop);

    ScopedStderrCapture capture;
    for (obj_data& drop : drops)
        obj_to_room(&drop, 0);
    const std::string output = capture.finish();

    EXPECT_EQ(FloorFixture::listed(), 1002u);
    EXPECT_EQ(count_occurrences(output, "obj_to_room:"), 1u) << output;
    EXPECT_NE(output.find("reached 1000 objects"), std::string::npos) << output;
}

// A player standing on room 0, with a descriptor whose output the test can read. Floors of more than
// about 300 objects used to fill the player's 16 KB output buffer, so everything sent after the objects
// in a look -- the people in the room -- was dropped.
class FloorViewer {
public:
    FloorViewer()
    {
        m_saved_light = world[0].light;
        world[0].light = 1;

        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;

        clear_char(&m_viewer, MOB_VOID);
        m_viewer.player.name = strdup("Looker");
        m_viewer.player.level = 10;
        m_viewer.in_room = 0;
        m_viewer.desc = &m_descriptor;
        m_descriptor.character = &m_viewer;

        static char description[] = "A rusty longsword lies here.";
        for (obj_data* object = world[0].contents; object; object = object->next_content)
            object->description = description;
    }

    ~FloorViewer()
    {
        if (m_descriptor.large_outbuf) {
            free(m_descriptor.large_outbuf->text);
            free(m_descriptor.large_outbuf);
        }
        free(m_viewer.player.name);
        world[0].light = m_saved_light;
    }

    std::string look_at_floor()
    {
        list_obj_to_char(world[0].contents, &m_viewer, 0, false);
        return m_descriptor.output;
    }

private:
    descriptor_data m_descriptor {};
    char_data m_viewer {};
    int m_saved_light = 0;
};

TEST(RoomContents, ALookAtACrowdedFloorListsAHundredObjectsAndCountsTheRest)
{
    FloorFixture fixture(150);
    FloorViewer viewer;

    const std::string output = viewer.look_at_floor();

    EXPECT_EQ(count_occurrences(output, "A rusty longsword lies here."), 100u);
    EXPECT_NE(output.find("...and 50 more items are lying here.\n\r"), std::string::npos) << output;
}

TEST(RoomContents, ALookAtAFloorOfAHundredListsEveryObject)
{
    FloorFixture fixture(100);
    FloorViewer viewer;

    const std::string output = viewer.look_at_floor();

    EXPECT_EQ(count_occurrences(output, "A rusty longsword lies here."), 100u);
    EXPECT_EQ(output.find("more item"), std::string::npos) << output;
}

TEST(RoomContents, ALookAtAFloorOfAHundredAndOneCountsTheLastItem)
{
    FloorFixture fixture(101);
    FloorViewer viewer;

    const std::string output = viewer.look_at_floor();

    EXPECT_EQ(count_occurrences(output, "A rusty longsword lies here."), 100u);
    EXPECT_NE(output.find("...and 1 more item is lying here.\n\r"), std::string::npos) << output;
}

} // namespace
