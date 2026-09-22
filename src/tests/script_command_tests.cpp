/* Crash guards in run_script (script.cpp) for two commands that used to
 * dereference a pointer they never checked.
 *
 * CHANGE_EXIT_TO wrote tmprm->dir_option[dir]->to_room with no check that the
 * exit exists (and its range check, -1 < dir < 6, is always true in C).
 * TELEPORT_CHAR_XL used tmprm->number with no check that the room is set.
 * Both crashed with SIGSEGV when reached from a mob's ON_DIE script.  A bad
 * line is now reported and skipped; a good line behaves exactly as before.
 */
#include "../handler.h"
#include "../protos.h"
#include "../script.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <vector>

int run_script(struct info_script* info, struct script_data* position);
void clear_char(struct char_data* ch, int mode);

extern struct room_data world;
extern int top_of_world;
extern struct descriptor_data* descriptor_list;
extern int rev_dir[];

namespace {

const int kRoomA = 0;
const int kRoomB = 1;
const int kRoomAVnum = 9100;
const int kRoomBVnum = 9101;

/* Rooms 0 and 1 as vnums 9100 and 9101 with no exits, put back afterwards. */
class TwoRooms {
public:
    TwoRooms()
    {
        if (room_data::BASE_WORLD == nullptr)
            world.create_bulk(2);
        m_saved_top = top_of_world;
        m_saved_descriptors = descriptor_list;
        descriptor_list = nullptr; // the reports mudlog; no stale descriptors from other tests
        top_of_world = kRoomB;
        for (int r : { kRoomA, kRoomB }) {
            m_saved[r].number = world[r].number;
            m_saved[r].people = world[r].people;
            for (int d = 0; d < NUM_OF_DIRS; ++d) {
                m_saved[r].exits[d] = world[r].dir_option[d];
                world[r].dir_option[d] = nullptr;
            }
            world[r].people = nullptr;
        }
        world[kRoomA].number = kRoomAVnum;
        world[kRoomB].number = kRoomBVnum;
    }

    ~TwoRooms()
    {
        for (int r : { kRoomA, kRoomB }) {
            world[r].number = m_saved[r].number;
            world[r].people = m_saved[r].people;
            for (int d = 0; d < NUM_OF_DIRS; ++d)
                world[r].dir_option[d] = m_saved[r].exits[d];
        }
        top_of_world = m_saved_top;
        descriptor_list = m_saved_descriptors;
    }

private:
    struct Saved {
        int number;
        char_data* people;
        room_direction_data* exits[NUM_OF_DIRS];
    } m_saved[2];
    int m_saved_top;
    descriptor_data* m_saved_descriptors;
};

/* A script as a linked list of commands numbered 1..n, ending in ABORT. */
class Script {
public:
    void add(int type, int p0 = 0, int p1 = 0, int p2 = 0)
    {
        script_data cmd {};
        cmd.command_type = type;
        cmd.param[0] = p0;
        cmd.param[1] = p1;
        cmd.param[2] = p2;
        m_nodes.push_back(cmd);
    }

    int run(info_script& info)
    {
        add(SCRIPT_ABORT);
        for (size_t i = 0; i < m_nodes.size(); ++i) {
            m_nodes[i].number = static_cast<int>(i) + 1;
            m_nodes[i].next = (i + 1 < m_nodes.size()) ? &m_nodes[i + 1] : nullptr;
            m_nodes[i].prev = (i > 0) ? &m_nodes[i - 1] : nullptr;
        }
        return run_script(&info, &m_nodes[0]);
    }

private:
    std::vector<script_data> m_nodes;
};

info_script make_info()
{
    info_script info {};
    info.index = -1; // not in script_table; reports print script #-1
    return info;
}

/* ---- CHANGE_EXIT_TO: RM1's exit <dir> now leads to <vnum> ---- */

TEST(ScriptChangeExitTo, ChangesAnExitThatExists)
{
    TwoRooms rooms;
    room_direction_data north {};
    north.to_room = kRoomA;
    world[kRoomA].dir_option[0] = &north;

    info_script info = make_info();
    info.rm[0] = &world[kRoomA];
    Script s;
    s.add(SCRIPT_CHANGE_EXIT_TO, SCRIPT_PARAM_RM1, 0, kRoomBVnum);
    s.run(info);

    EXPECT_EQ(kRoomB, north.to_room);
    world[kRoomA].dir_option[0] = nullptr;
}

TEST(ScriptChangeExitTo, SkipsAnExitThatDoesNotExist)
{
    TwoRooms rooms;
    info_script info = make_info();
    info.rm[0] = &world[kRoomA];
    Script s;
    s.add(SCRIPT_CHANGE_EXIT_TO, SCRIPT_PARAM_RM1, 0, kRoomBVnum);
    s.run(info); // used to write through a null exit

    EXPECT_EQ(nullptr, world[kRoomA].dir_option[0]);
}

TEST(ScriptChangeExitTo, SkipsADirectionOutsideTheSix)
{
    TwoRooms rooms;
    info_script info = make_info();
    info.rm[0] = &world[kRoomA];
    Script s;
    s.add(SCRIPT_CHANGE_EXIT_TO, SCRIPT_PARAM_RM1, NUM_OF_DIRS, kRoomBVnum);
    s.add(SCRIPT_CHANGE_EXIT_TO, SCRIPT_PARAM_RM1, -1, kRoomBVnum);
    s.run(info); // used to index past either end of dir_option

    for (int d = 0; d < NUM_OF_DIRS; ++d)
        EXPECT_EQ(nullptr, world[kRoomA].dir_option[d]);
}

/* ---- SET_EXIT_STATE <dir> <state> <room>: open/close/lock a door and its
 * other side.  Direction 0 is north, a real direction, but the command used
 * to treat a 0 as "not set" and silently do nothing. ---- */

/* A closed door from room A to room B in <dir>, and back again. */
class DoorBetweenRooms {
public:
    explicit DoorBetweenRooms(int dir)
        : m_dir(dir)
    {
        m_there.exit_info = EX_ISDOOR | EX_CLOSED;
        m_there.to_room = kRoomB;
        m_back.exit_info = EX_ISDOOR | EX_CLOSED;
        m_back.to_room = kRoomA;
        world[kRoomA].dir_option[dir] = &m_there;
        world[kRoomB].dir_option[rev_dir[dir]] = &m_back;
    }
    ~DoorBetweenRooms()
    {
        world[kRoomA].dir_option[m_dir] = nullptr;
        world[kRoomB].dir_option[rev_dir[m_dir]] = nullptr;
    }

    room_direction_data m_there {};
    room_direction_data m_back {};

private:
    int m_dir;
};

void open_door(int dir)
{
    info_script info = make_info();
    info.rm[0] = &world[kRoomA];
    Script s;
    s.add(SCRIPT_SET_EXIT_STATE, dir, 0, SCRIPT_PARAM_RM1);
    s.run(info);
}

TEST(ScriptSetExitState, OpensADoorToTheEast)
{
    TwoRooms rooms;
    DoorBetweenRooms door(1);
    open_door(1);
    EXPECT_FALSE(IS_SET(door.m_there.exit_info, EX_CLOSED));
    EXPECT_FALSE(IS_SET(door.m_back.exit_info, EX_CLOSED));
}

TEST(ScriptSetExitState, OpensADoorToTheNorth)
{
    TwoRooms rooms;
    DoorBetweenRooms door(0);
    open_door(0);
    EXPECT_FALSE(IS_SET(door.m_there.exit_info, EX_CLOSED));
    EXPECT_FALSE(IS_SET(door.m_back.exit_info, EX_CLOSED));
}

TEST(ScriptSetExitState, SkipsADirectionOutsideTheSix)
{
    TwoRooms rooms;
    DoorBetweenRooms door(0);
    open_door(NUM_OF_DIRS);
    open_door(-1);
    EXPECT_TRUE(IS_SET(door.m_there.exit_info, EX_CLOSED));
    EXPECT_TRUE(IS_SET(door.m_back.exit_info, EX_CLOSED));
}

/* ---- TELEPORT_CHAR_XL: move CH1 to <room variable> ---- */

/* TELEPORT_CHAR_XL runs only when run_script's tmpint, left by an earlier
 * command, is not negative (an old quirk, deliberately unchanged).  A
 * TELEPORT_CHAR on an unset character looks up a room that exists and moves
 * no one, which leaves tmpint >= 0 so the XL line is reached. */
void reach_teleport_xl(Script& s)
{
    s.add(SCRIPT_TELEPORT_CHAR, kRoomBVnum, SCRIPT_PARAM_CH2);
}

class TeleportXL : public ::testing::Test {
protected:
    void SetUp() override
    {
        clear_char(&m_ch, MOB_VOID);
        char_to_room(&m_ch, kRoomA);
    }
    void TearDown() override { char_from_room(&m_ch); }

    TwoRooms m_rooms;
    char_data m_ch {};
};

TEST_F(TeleportXL, MovesTheCharacterToASetRoom)
{
    info_script info = make_info();
    info.ch[0] = &m_ch;
    info.rm[0] = &world[kRoomB];
    Script s;
    reach_teleport_xl(s);
    s.add(SCRIPT_TELEPORT_CHAR_XL, SCRIPT_PARAM_RM1, SCRIPT_PARAM_CH1);
    s.run(info);

    EXPECT_EQ(kRoomB, m_ch.in_room);
}

TEST_F(TeleportXL, SkipsARoomVariableThatIsNotSet)
{
    info_script info = make_info();
    info.ch[0] = &m_ch;
    Script s;
    reach_teleport_xl(s);
    s.add(SCRIPT_TELEPORT_CHAR_XL, SCRIPT_PARAM_RM1, SCRIPT_PARAM_CH1);
    s.run(info); // used to read the number of a null room

    EXPECT_EQ(kRoomA, m_ch.in_room);
}

TEST_F(TeleportXL, SkipsTheRoomOfACharacterThatIsNotSet)
{
    info_script info = make_info();
    info.ch[0] = &m_ch;
    Script s;
    reach_teleport_xl(s);
    s.add(SCRIPT_TELEPORT_CHAR_XL, SCRIPT_PARAM_CH3_ROOM, SCRIPT_PARAM_CH1);
    s.run(info);

    EXPECT_EQ(kRoomA, m_ch.in_room);
}

} // namespace
