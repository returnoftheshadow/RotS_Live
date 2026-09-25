/* Crash guards in run_script (script.cpp) for two commands that used to
 * dereference a pointer they never checked.
 *
 * CHANGE_EXIT_TO wrote tmprm->dir_option[dir]->to_room with no check that the
 * exit exists (and its range check, -1 < dir < 6, is always true in C).
 * TELEPORT_CHAR_XL used tmprm->number with no check that the room is set.
 * Both crashed with SIGSEGV when reached from a mob's ON_DIE script.  A bad
 * line is now reported and skipped; a good line behaves exactly as before.
 */
#include "../db.h"
#include "../handler.h"
#include "../protos.h"
#include "../script.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <vector>

int run_script(struct info_script* info, struct script_data* position);
void clear_object(struct obj_data* obj);
void clear_char(struct char_data* ch, int mode);

extern struct room_data world;
extern int top_of_world;
extern struct descriptor_data* descriptor_list;
extern int rev_dir[];
extern struct index_data* obj_index;
extern struct obj_data* obj_proto;
extern struct obj_data* object_list;
extern int top_of_objt;

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

    void set_param(size_t index, int param, int value) { m_nodes[index].param[param] = value; }

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

/* ---- DO_REMOVE <ch> <slot> and ASSIGN_EQ <ch> <obj> <slot> <int>: equipment
 * slots run 0 to MAX_WEAR - 1.  DO_REMOVE used to treat slot 0 (the light) as
 * "not set", and neither checked the slot before indexing equipment[]. ---- */

/* A character standing in room A wearing one object in <slot>. */
class WornObject : public ::testing::Test {
protected:
    void SetUp() override
    {
        clear_char(&m_ch, MOB_VOID);
        char_to_room(&m_ch, kRoomA);
        clear_object(&m_obj);
        m_obj.in_room = NOWHERE;
        m_obj.obj_flags.type_flag = ITEM_OTHER;
        m_obj.short_description = m_short;
    }
    void TearDown() override
    {
        for (int slot = 0; slot < MAX_WEAR; ++slot)
            if (m_ch.equipment[slot])
                unequip_char(&m_ch, slot);
        if (m_obj.carried_by)
            obj_from_char(&m_obj);
        char_from_room(&m_ch);
    }
    void wear(int slot) { equip_char(&m_ch, &m_obj, slot); }

    TwoRooms m_rooms;
    char_data m_ch {};
    obj_data m_obj {};
    char m_short[16] = "a test object";
};

void remove_slot(char_data* ch, int slot)
{
    info_script info = make_info();
    info.ch[0] = ch;
    Script s;
    s.add(SCRIPT_DO_REMOVE, SCRIPT_PARAM_CH1, slot);
    s.run(info);
}

TEST_F(WornObject, DoRemoveTakesOffABodySlot)
{
    wear(WEAR_BODY);
    remove_slot(&m_ch, WEAR_BODY);
    EXPECT_EQ(nullptr, m_ch.equipment[WEAR_BODY]);
    EXPECT_EQ(&m_ch, m_obj.carried_by);
}

TEST_F(WornObject, DoRemoveTakesOffTheLight)
{
    wear(WEAR_LIGHT);
    remove_slot(&m_ch, WEAR_LIGHT);
    EXPECT_EQ(nullptr, m_ch.equipment[WEAR_LIGHT]);
    EXPECT_EQ(&m_ch, m_obj.carried_by);
}

TEST_F(WornObject, DoRemoveSkipsASlotOutsideTheRange)
{
    wear(WEAR_BODY);
    remove_slot(&m_ch, MAX_WEAR);
    remove_slot(&m_ch, -1);
    EXPECT_EQ(&m_obj, m_ch.equipment[WEAR_BODY]);
}

/* Runs ASSIGN_EQ ch1 -> ob1 for <slot>, with int1 recording whether it found
 * something.  int1 starts at 7 so a line that never writes it is visible: an
 * out-of-range slot is a miss and must write 0, "not found". */
info_script assign_slot(char_data* ch, int slot)
{
    info_script info = make_info();
    info.ch[0] = ch;
    info.ints[0] = 7;
    Script s;
    s.add(SCRIPT_ASSIGN_EQ, SCRIPT_PARAM_CH1, SCRIPT_PARAM_OB1, slot);
    s.set_param(0, 3, SCRIPT_PARAM_INT1);
    s.run(info);
    return info;
}

TEST_F(WornObject, AssignEqFindsTheObjectInASlot)
{
    wear(WEAR_BODY);
    info_script info = assign_slot(&m_ch, WEAR_BODY);
    EXPECT_EQ(&m_obj, info.ob[0]);
    EXPECT_EQ(1, info.ints[0]);
}

TEST_F(WornObject, AssignEqSkipsASlotOutsideTheRange)
{
    wear(WEAR_BODY);
    info_script info = assign_slot(&m_ch, MAX_WEAR);
    EXPECT_EQ(nullptr, info.ob[0]);
    EXPECT_EQ(0, info.ints[0]);
    info = assign_slot(&m_ch, -1);
    EXPECT_EQ(nullptr, info.ob[0]);
    EXPECT_EQ(0, info.ints[0]);
}

/* ASSIGN_EQ with its character variable unset must find nothing.  It used to
 * keep whatever object an earlier line of the same run had touched and hand
 * that to the script as "found".  Here DO_DROP (also with an unset character)
 * is the earlier line: it looks up ob2 and drops nothing. */
TEST_F(WornObject, AssignEqWithAnUnsetCharacterFindsNothing)
{
    info_script info = make_info();
    info.ob[1] = &m_obj;
    info.ints[0] = 7;
    Script s;
    s.add(SCRIPT_DO_DROP, SCRIPT_PARAM_CH2, SCRIPT_PARAM_OB2);
    s.add(SCRIPT_ASSIGN_EQ, SCRIPT_PARAM_CH2, SCRIPT_PARAM_OB1, WEAR_BODY);
    s.set_param(1, 3, SCRIPT_PARAM_INT1);
    s.run(info);

    EXPECT_EQ(nullptr, info.ob[0]);
    EXPECT_EQ(0, info.ints[0]);
}

/* EQUIP_CHAR <ch> <vnum1..vnum5>: a 0 slot is empty.  Real number 0 became a
 * valid lookup result (the first object in the table must be loadable), so an
 * empty slot must be skipped before it is looked up -- otherwise, if an object
 * with vnum 0 ever exists, every empty slot loads a copy of it. */
class ObjectTableWithVnumZero : public WornObject {
protected:
    void SetUp() override
    {
        WornObject::SetUp();
        m_saved_index = obj_index;
        m_saved_proto = obj_proto;
        m_saved_top = top_of_objt;
        m_saved_list = object_list;
        m_index[0].virt = 0;
        clear_object(&m_proto[0]);
        m_proto[0].in_room = NOWHERE;
        m_proto[0].obj_flags.type_flag = ITEM_OTHER;
        m_proto[0].short_description = m_short;
        m_proto[0].name = m_short;
        obj_index = m_index;
        obj_proto = m_proto;
        top_of_objt = 0;
    }
    void TearDown() override
    {
        while (obj_data* o = m_ch.carrying) {
            obj_from_char(o);
            free(o);
        }
        object_list = m_saved_list;
        obj_index = m_saved_index;
        obj_proto = m_saved_proto;
        top_of_objt = m_saved_top;
        WornObject::TearDown();
    }
    int carried()
    {
        int n = 0;
        for (obj_data* o = m_ch.carrying; o; o = o->next_content)
            ++n;
        for (int slot = 0; slot < MAX_WEAR; ++slot)
            if (m_ch.equipment[slot] && m_ch.equipment[slot] != &m_obj)
                ++n;
        return n;
    }

    index_data m_index[1] {};
    obj_data m_proto[1] {};
    index_data* m_saved_index;
    obj_data* m_saved_proto;
    int m_saved_top;
    obj_data* m_saved_list;
};

TEST_F(ObjectTableWithVnumZero, EquipCharSkipsEmptySlots)
{
    info_script info = make_info();
    info.ch[0] = &m_ch;
    Script s;
    s.add(SCRIPT_EQUIP_CHAR, SCRIPT_PARAM_CH1);
    s.run(info);
    EXPECT_EQ(0, carried());
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
