#include "../structs.h"

#include <gtest/gtest.h>

extern struct room_data world;
extern int top_of_world;
void recount_light_room(int room);

namespace {

// The light value a room holds before a recount, so a test can tell an overwrite from no change.
constexpr int kLightBeforeRecount = 7;

class RecountLightRoomTest : public testing::Test {
protected:
    void SetUp() override
    {
        if (room_data::BASE_WORLD == nullptr) {
            world.create_bulk(1);
        }

        m_saved_top_of_world = top_of_world;
        m_saved_people = world[0].people;
        m_saved_contents = world[0].contents;
        m_saved_light = world[0].light;

        top_of_world = 1;
        world[0].people = nullptr;
        world[0].contents = nullptr;
        world[0].light = kLightBeforeRecount;
    }

    void TearDown() override
    {
        world[0].light = m_saved_light;
        world[0].contents = m_saved_contents;
        world[0].people = m_saved_people;
        top_of_world = m_saved_top_of_world;
    }

    // Makes `item` a light with `hours_left` hours of fuel (negative never burns out).
    static void make_light(obj_data& item, int hours_left, bool lit)
    {
        item.obj_flags.type_flag = ITEM_LIGHT;
        item.obj_flags.value[2] = hours_left;
        item.obj_flags.value[3] = 0;
        if (lit) {
            item.obj_flags.value[3] = 1;
        }
    }

    // Puts `person` at the head of room 0's people, as char_to_room does.
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
    // Room 0's people list before SetUp emptied it.
    char_data* m_saved_people = nullptr;
    // Room 0's floor contents before SetUp emptied it.
    obj_data* m_saved_contents = nullptr;
    // Room 0's light value before SetUp replaced it.
    byte m_saved_light = 0;

    // People a test stands in room 0.
    char_data m_first_person { };
    // A second person, for tests that need the whole people list walked.
    char_data m_second_person { };
    // A third person, so the list has a middle as well as both ends.
    char_data m_third_person { };

    // Objects a test equips, carries or drops.
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
    // Unlike worn items, floor objects are counted on value[2] and value[3] alone.
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

TEST_F(RecountLightRoomTest, LeavesTheLightAloneForARoomOutsideTheWorld)
{
    make_light(m_first_item, 10, true);
    drop_on_floor(m_first_item);

    recount_light_room(-1);
    recount_light_room(top_of_world);

    EXPECT_EQ(kLightBeforeRecount, world[0].light);
}

} // namespace
