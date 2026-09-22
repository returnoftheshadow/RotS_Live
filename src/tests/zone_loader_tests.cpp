/* load_zones (zone.cpp) reading a zone file's command list.
 *
 * Each command line ends in a free-text comment.  load_zones used to read it
 * with a single fgets(buf, 80), so a longer comment left its tail unread and
 * the next pass took the tail as a command: a phantom command when the tail
 * began with text, or -- when the tail was only the line end -- a blank
 * command that swallowed the next real line whole.  Live world files had four
 * lines lost this way (58, 107, 200, 242.zon).
 *
 * The files are written with CRLF line ends, as the live world files are:
 * the swallowed-line case depends on the '\r' filling the buffer exactly.
 */
#include "../db.h"
#include "../structs.h"
#include "../zone.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

/* A one-zone file: header, then the given command lines, then S. */
std::string zone_file(const std::string& commands)
{
    return "#9001 Test zone~\r\n"
           "A zone for tests.\r\n~\r\n"
           "~\r\n"
           "0\r\n"
           "x 1 2 3\r\n"
           "9099\r\n"
           "10\r\n"
           "2\r\n"
        + commands + "S\r\n";
}

/* Loads one zone from text into a scratch zone_table and exposes it.
 * load_zones keeps its own static zone index, so the table is sized for
 * several loads and the loaded zone is found through top_of_zone_table. */
class LoadedZone {
public:
    explicit LoadedZone(const std::string& text)
        : m_saved_table(zone_table)
        , m_saved_top(top_of_zone_table)
    {
        zone_table = m_table;
        FILE* f = std::tmpfile();
        EXPECT_NE(f, nullptr);
        std::fputs(text.c_str(), f);
        std::rewind(f);
        load_zones(f);
        std::fclose(f);
        m_zone = &zone_table[top_of_zone_table];
    }

    ~LoadedZone()
    {
        std::free(m_zone->cmd);
        std::free(m_zone->name);
        std::free(m_zone->description);
        std::free(m_zone->map);
        for (owner_list* o = m_zone->owners; o;) {
            owner_list* next = o->next;
            std::free(o);
            o = next;
        }
        zone_table = m_saved_table;
        top_of_zone_table = m_saved_top;
    }

    const zone_data& zone() const { return *m_zone; }

private:
    zone_data m_table[16] {};
    zone_data* m_zone;
    zone_data* m_saved_table;
    int m_saved_top;
};

const std::string kNextLine = "O 0 200 9001 0 100 1 next line\r\n";

TEST(LoadZones, ReadsEveryCommandWhenCommentsAreShort)
{
    LoadedZone z(zone_file("M 0 100 9001 0 100 100 1 1 a mob\r\n" + kNextLine));
    ASSERT_EQ(2, z.zone().cmdno);
    EXPECT_EQ('M', z.zone().cmd[0].command);
    EXPECT_EQ('O', z.zone().cmd[1].command);
    EXPECT_EQ(200, z.zone().cmd[1].arg1);
}

TEST(LoadZones, ACommentThatExactlyFillsTheBufferDoesNotSwallowTheNextLine)
{
    // After the last number, " " + comment + "\r" is 79 characters -- all a
    // single 80-byte fgets reads -- leaving only the '\n' behind.
    std::string comment(77, 'c');
    LoadedZone z(zone_file("M 0 100 9001 0 100 100 1 1 " + comment + "\r\n" + kNextLine));
    ASSERT_EQ(2, z.zone().cmdno);
    EXPECT_EQ('M', z.zone().cmd[0].command);
    EXPECT_EQ('O', z.zone().cmd[1].command);
    EXPECT_EQ(200, z.zone().cmd[1].arg1);
}

TEST(LoadZones, ALongCommentDoesNotBecomeAPhantomCommand)
{
    std::string comment = "give the blacksmith his hammer " + std::string(150, 'x');
    LoadedZone z(zone_file("M 0 100 9001 0 100 100 1 1 " + comment + "\r\n" + kNextLine));
    ASSERT_EQ(2, z.zone().cmdno);
    EXPECT_EQ('M', z.zone().cmd[0].command);
    EXPECT_EQ('O', z.zone().cmd[1].command);
    EXPECT_EQ(200, z.zone().cmd[1].arg1);
}

} // namespace
