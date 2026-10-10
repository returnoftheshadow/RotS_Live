#include "../structs.h"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

extern struct room_data world;
extern int top_of_world;
void recount_light_room(int room);

namespace {

// The light value a room holds before a recount, so a test can tell an overwrite from no change.
constexpr int LIGHT_BEFORE_RECOUNT = 7;

// The room the fixture's top_of_world puts just outside the world.
constexpr int ROOM_PAST_THE_WORLD = 1;

class RecountLightRoomTest : public testing::Test {
protected:
    void SetUp() override
    {
        if (room_data::BASE_WORLD == nullptr) {
            world.create_bulk(1);
        }

        m_saved_top_of_world = top_of_world;
        m_saved_room = save_and_clear(0);
        m_saved_room_past_the_world = save_and_clear(ROOM_PAST_THE_WORLD);
        top_of_world = ROOM_PAST_THE_WORLD;
    }

    void TearDown() override
    {
        top_of_world = m_saved_top_of_world;
        restore(ROOM_PAST_THE_WORLD, m_saved_room_past_the_world);
        restore(0, m_saved_room);
    }

    // A room's lists and light as SetUp found them.
    struct SavedRoom {
        // Head of the room's people list when SetUp ran; restore puts it back.
        char_data* people = nullptr;
        // Head of the room's floor contents when SetUp ran; restore puts it back.
        obj_data* contents = nullptr;
        // The room's light value when SetUp ran; restore puts it back.
        byte light = 0;
    };

    // Saves `room`'s lists and light, then empties the lists and sets the light to
    // LIGHT_BEFORE_RECOUNT.
    static SavedRoom save_and_clear(int room)
    {
        SavedRoom saved;
        saved.people = world[room].people;
        saved.contents = world[room].contents;
        saved.light = world[room].light;
        world[room].people = nullptr;
        world[room].contents = nullptr;
        world[room].light = LIGHT_BEFORE_RECOUNT;
        return saved;
    }

    // Puts back what save_and_clear saved from `room`.
    static void restore(int room, const SavedRoom& saved)
    {
        world[room].people = saved.people;
        world[room].contents = saved.contents;
        world[room].light = saved.light;
    }

    // Makes `item` a light with `hours_left` hours of fuel (negative never burns out), lit when
    // `lit` is true.
    static void make_light(obj_data& item, int hours_left, bool lit)
    {
        item.obj_flags.type_flag = ITEM_LIGHT;
        item.obj_flags.value[2] = hours_left;
        item.obj_flags.value[3] = 0;
        if (lit) {
            item.obj_flags.value[3] = 1;
        }
    }

    // Puts `person` at the head of room 0's people. char_to_room appends at the tail instead,
    // which a count does not notice.
    static void enter_room(char_data& person)
    {
        person.next_in_room = world[0].people;
        world[0].people = &person;
    }

    // Puts `item` at the head of room 0's floor contents, as obj_to_room does.
    static void drop_on_floor(obj_data& item)
    {
        item.next_content = world[0].contents;
        world[0].contents = &item;
    }

    // top_of_world before SetUp made room 0 the only room in range; TearDown restores it.
    int m_saved_top_of_world = 0;
    // Room 0's lists and light before SetUp cleared them.
    SavedRoom m_saved_room;
    // ROOM_PAST_THE_WORLD's lists and light before SetUp cleared them.
    SavedRoom m_saved_room_past_the_world;

    // A person a test stands in room 0.
    char_data m_first_person { };
    // A second person, for tests that need the whole people list walked.
    char_data m_second_person { };
    // A third person, so the list has a middle as well as both ends.
    char_data m_third_person { };

    // An object a test equips, carries or drops.
    obj_data m_first_item { };
    // A second object, for tests that count more than one.
    obj_data m_second_item { };
    // A third object, for tests that count more than two.
    obj_data m_third_item { };
};

TEST_F(RecountLightRoomTest, CountsNothingInAnEmptyRoom)
{
    recount_light_room(0);

    EXPECT_EQ(0, world[0].light) << "an empty room's light is overwritten with 0";
}

TEST_F(RecountLightRoomTest, CountsALitLightWornBySomeoneInTheRoom)
{
    make_light(m_first_item, 10, true);
    m_first_person.equipment[WEAR_LIGHT] = &m_first_item;
    enter_room(m_first_person);

    recount_light_room(0);

    EXPECT_EQ(1, world[0].light);
}

TEST_F(RecountLightRoomTest, CountsEveryLitLightOnePersonWears)
{
    make_light(m_first_item, 10, true);
    make_light(m_second_item, 10, true);
    m_first_person.equipment[WEAR_LIGHT] = &m_first_item;
    m_first_person.equipment[WEAR_HEAD] = &m_second_item;
    enter_room(m_first_person);

    recount_light_room(0);

    EXPECT_EQ(2, world[0].light);
}

TEST_F(RecountLightRoomTest, CountsLightsWornByEveryoneInTheRoom)
{
    make_light(m_first_item, 10, true);
    make_light(m_second_item, 10, true);
    m_first_person.equipment[WEAR_LIGHT] = &m_first_item;
    m_third_person.equipment[WEAR_LIGHT] = &m_second_item;
    enter_room(m_third_person);
    enter_room(m_second_person);
    enter_room(m_first_person);

    recount_light_room(0);

    EXPECT_EQ(2, world[0].light) << "lights on the first and last of three people both count";
}

TEST_F(RecountLightRoomTest, IgnoresAWornLightThatIsNotLit)
{
    make_light(m_first_item, 10, false);
    m_first_person.equipment[WEAR_LIGHT] = &m_first_item;
    enter_room(m_first_person);

    recount_light_room(0);

    EXPECT_EQ(0, world[0].light);
}

TEST_F(RecountLightRoomTest, IgnoresAWornLightWithNoHoursLeft)
{
    make_light(m_first_item, 0, true);
    m_first_person.equipment[WEAR_LIGHT] = &m_first_item;
    enter_room(m_first_person);

    recount_light_room(0);

    EXPECT_EQ(0, world[0].light);
}

TEST_F(RecountLightRoomTest, CountsAWornLightThatNeverBurnsOut)
{
    make_light(m_first_item, -1, true);
    m_first_person.equipment[WEAR_LIGHT] = &m_first_item;
    enter_room(m_first_person);

    recount_light_room(0);

    EXPECT_EQ(1, world[0].light);
}

TEST_F(RecountLightRoomTest, IgnoresAWornItemThatIsNotALight)
{
    m_first_item.obj_flags.type_flag = ITEM_WEAPON;
    m_first_item.obj_flags.value[2] = 4;
    m_first_item.obj_flags.value[3] = 3;
    m_first_person.equipment[WEAR_BODY] = &m_first_item;
    enter_room(m_first_person);

    recount_light_room(0);

    EXPECT_EQ(0, world[0].light);
}

TEST_F(RecountLightRoomTest, IgnoresALitLightCarriedRatherThanWorn)
{
    make_light(m_first_item, 10, true);
    m_first_person.carrying = &m_first_item;
    enter_room(m_first_person);

    recount_light_room(0);

    EXPECT_EQ(0, world[0].light);
}

TEST_F(RecountLightRoomTest, CountsALitLightOnTheFloor)
{
    make_light(m_first_item, 10, true);
    drop_on_floor(m_first_item);

    recount_light_room(0);

    EXPECT_EQ(1, world[0].light);
}

TEST_F(RecountLightRoomTest, IgnoresAnUnlitLightOnTheFloor)
{
    make_light(m_first_item, 10, false);
    drop_on_floor(m_first_item);

    recount_light_room(0);

    EXPECT_EQ(0, world[0].light);
}

TEST_F(RecountLightRoomTest, CountsAFloorObjectOfAnyTypeWithItsLightValuesSet)
{
    m_first_item.obj_flags.type_flag = ITEM_WEAPON;
    m_first_item.obj_flags.value[2] = 4;
    m_first_item.obj_flags.value[3] = 3;
    drop_on_floor(m_first_item);

    recount_light_room(0);

    EXPECT_EQ(1, world[0].light);
}

TEST_F(RecountLightRoomTest, IgnoresALitLightInsideAContainerOnTheFloor)
{
    make_light(m_first_item, 10, true);
    m_second_item.obj_flags.type_flag = ITEM_CONTAINER;
    m_second_item.contains = &m_first_item;
    m_first_item.in_obj = &m_second_item;
    drop_on_floor(m_second_item);

    recount_light_room(0);

    EXPECT_EQ(0, world[0].light);
}

TEST_F(RecountLightRoomTest, AddsWornAndFloorLightsTogether)
{
    make_light(m_first_item, 10, true);
    make_light(m_second_item, 10, true);
    make_light(m_third_item, -1, true);
    m_first_person.equipment[WEAR_LIGHT] = &m_first_item;
    enter_room(m_first_person);
    drop_on_floor(m_second_item);
    drop_on_floor(m_third_item);

    recount_light_room(0);

    EXPECT_EQ(3, world[0].light) << "one worn light plus both lights on the floor";
}

TEST_F(RecountLightRoomTest, WrapsACountPastTheRangeOfALightValue)
{
    // Two past the largest light value, so the count wraps to 1.
    constexpr int LIT_FLOOR_OBJECTS = std::numeric_limits<byte>::max() + 2;
    std::vector<obj_data> floor_lights(LIT_FLOOR_OBJECTS);
    for (obj_data& item : floor_lights) {
        make_light(item, 10, true);
        drop_on_floor(item);
    }

    recount_light_room(0);

    EXPECT_EQ(1, world[0].light) << LIT_FLOOR_OBJECTS << " lit floor objects wrap in a byte";
}

TEST_F(RecountLightRoomTest, LeavesTheLightAloneForARoomOutsideTheWorld)
{
    make_light(m_first_item, 10, true);
    drop_on_floor(m_first_item);

    recount_light_room(-1);
    recount_light_room(top_of_world);

    EXPECT_EQ(LIGHT_BEFORE_RECOUNT, world[0].light) << "room -1 is not recounted as room 0";
    EXPECT_EQ(LIGHT_BEFORE_RECOUNT, world[ROOM_PAST_THE_WORLD].light)
        << "room top_of_world is not recounted";
}

} // namespace
