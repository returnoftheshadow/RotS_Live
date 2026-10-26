/* An archer's shot takes its arrow from the quiver it wears, or, for an NPC without one, from its
 * own inventory, and moves it to the one it hits or to the floor. The arrow ends on exactly one
 * list, and nothing the archer does not carry is touched. The followers here shoot when their
 * leader orders them to, as players have them do. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../skill_timer.h"

void do_order(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void assign_command_pointers(void);
void complete_delay(struct char_data* ch);

extern struct weather_data weather_info;

using namespace scripted_room;

namespace {

/* More objects than any test puts on one list: a walk this long has gone round a cycle. */
constexpr int LIST_WALK_LIMIT = 100;
/* The shot's roll, number(0, 99) against an NPC's archery of 80: a sure hit and a sure miss. */
constexpr double SURE_HIT_ROLL = 0.0;
constexpr double SURE_MISS_ROLL = 0.99;
/* A missed arrow's roll above 0.21 lands it in the archer's room. */
constexpr double LANDS_IN_THE_ROOM_ROLL = 0.99;
constexpr const char* OUT_OF_ARROWS_MESSAGE = "Your follower is out of arrows.";

class RangerShootTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        /* Made at boot; a shot asks them. Making them again keeps the ones there are. */
        game_rules::big_brother::create(weather_info, &world);
        game_timer::skill_timer::create(weather_info, &world);
        assign_command_pointers();
    }

    /* An arrow, not placed. */
    obj_data* make_arrow()
    {
        obj_data* arrow = make_object(STONE_VNUM, 0);
        arrow->name = m_arrow_keywords;
        arrow->obj_flags.type_flag = ITEM_MISSILE;
        return arrow;
    }

    /* A quiver, not placed. */
    obj_data* make_quiver()
    {
        obj_data* quiver = make_object(BAG_VNUM, 0);
        quiver->name = m_quiver_keywords;
        return quiver;
    }

    /* An NPC following the player under its orders, wielding a bow in both hands. */
    char_data* make_archer()
    {
        char_data* archer = make_mob("archer", 0);
        /* A charmed follower's damage is recorded; clear_char zeroed the record's map. */
        new (&archer->damage_details) player_damage_details();
        add_follower(archer, &m_player, FOLLOW_MOVE);
        SET_BIT(archer->specials.affected_by, AFF_CHARM);
        obj_data* bow = make_object(SWORD_VNUM, 0);
        bow->name = m_bow_keywords;
        equip_char(archer, bow, WIELD);
        SET_BIT(archer->specials.affected_by, AFF_TWOHANDED);
        return archer;
    }

    /* The player orders <archer> to shoot the mob called "target"; the shot is loosed once the
     * draw's delay is over. */
    void order_shot(char_data* archer)
    {
        run_command(do_order, "archer shoot target");
        if (archer->delay.wait_value > 0) {
            complete_delay(archer);
        }
    }

    /* How many objects are on the next_content list that starts with <first>, stopping at
     * LIST_WALK_LIMIT. */
    static int count_contents_bounded(const obj_data* first)
    {
        int count = 0;
        for (const obj_data* content = first; content && count < LIST_WALK_LIMIT;
             content = content->next_content) {
            ++count;
        }
        return count;
    }

    /* How many times <object> is on the next_content list that starts with <first>, looking at
     * no more than LIST_WALK_LIMIT entries. */
    static int times_listed(const obj_data* first, const obj_data* object)
    {
        int times = 0;
        int walked = 0;
        for (const obj_data* content = first; content && walked < LIST_WALK_LIMIT;
             content = content->next_content, ++walked) {
            if (content == object) {
                ++times;
            }
        }
        return times;
    }

    // The keywords that make the test objects an arrow, a bow and a quiver.
    char m_arrow_keywords[16] = "arrow";
    char m_bow_keywords[16] = "bow long";
    char m_quiver_keywords[16] = "quiver leather";
};

} // namespace

/* A follower with no quiver shoots the arrow it carries, which ends with the one it hits and on
 * no other list. */
TEST_F(RangerShootTest, AFollowerShootsTheArrowInItsInventoryIntoItsTarget)
{
    char_data* archer = make_archer();
    char_data* target = make_mob("target", 0);
    obj_data* arrow = make_arrow();
    obj_to_char(arrow, archer);
    push_test_random_value(SURE_HIT_ROLL);

    order_shot(archer);

    EXPECT_EQ(target, arrow->carried_by);
    EXPECT_EQ(1, times_listed(target->carrying, arrow));
    EXPECT_EQ(0, count_contents_bounded(archer->carrying));
    EXPECT_EQ(0, times_listed(world[0].contents, arrow));
    EXPECT_EQ(archer->abs_number, arrow->obj_flags.value[2]) << "tagged as the archer's";
}

/* A follower's missed arrow falls to the floor and leaves its inventory. */
TEST_F(RangerShootTest, AFollowersMissedArrowFromItsInventoryFallsToTheFloor)
{
    char_data* archer = make_archer();
    char_data* target = make_mob("target", 0);
    obj_data* arrow = make_arrow();
    obj_data* bread = make_object(BREAD_VNUM, 0);
    obj_to_char(bread, archer);
    obj_to_char(arrow, archer);
    push_test_random_value(SURE_MISS_ROLL);
    push_test_random_value(LANDS_IN_THE_ROOM_ROLL);

    order_shot(archer);

    EXPECT_EQ(nullptr, arrow->carried_by);
    EXPECT_EQ(1, times_listed(world[0].contents, arrow));
    EXPECT_EQ(1, count_contents_bounded(archer->carrying));
    EXPECT_EQ(bread, archer->carrying);
    EXPECT_EQ(0, times_listed(target->carrying, arrow));
}

/* A follower carrying no arrow does not shoot one someone else carries: it is out of arrows, and
 * the other arrow stays where it is. */
TEST_F(RangerShootTest, AFollowerWithNoArrowDoesNotShootOneItDoesNotCarry)
{
    obj_data* players_arrow = make_arrow();
    obj_to_char(players_arrow, &m_player);
    char_data* archer = make_archer();
    char_data* target = make_mob("target", 0);
    obj_to_char(make_object(BREAD_VNUM, 0), archer);

    order_shot(archer);

    EXPECT_TRUE(output_has(OUT_OF_ARROWS_MESSAGE));
    EXPECT_EQ(&m_player, players_arrow->carried_by);
    EXPECT_EQ(players_arrow, m_player.carrying);
    EXPECT_EQ(1, count_contents_bounded(m_player.carrying));
    EXPECT_EQ(1, count_contents_bounded(archer->carrying));
    EXPECT_EQ(nullptr, target->carrying);
}

/* A follower wearing a quiver shoots from it, as a player does. */
TEST_F(RangerShootTest, AFollowerWithAQuiverShootsFromIt)
{
    char_data* archer = make_archer();
    char_data* target = make_mob("target", 0);
    obj_data* quiver = make_quiver();
    obj_data* arrow = make_arrow();
    obj_to_obj(arrow, quiver);
    equip_char(archer, quiver, WEAR_BACK);
    obj_data* spare_arrow = make_arrow();
    obj_to_char(spare_arrow, archer);
    push_test_random_value(SURE_HIT_ROLL);

    order_shot(archer);

    EXPECT_EQ(target, arrow->carried_by);
    EXPECT_EQ(nullptr, arrow->in_obj);
    EXPECT_EQ(1, times_listed(target->carrying, arrow));
    EXPECT_EQ(nullptr, quiver->contains);
    EXPECT_EQ(spare_arrow, archer->carrying) << "the arrow in its inventory is not used";
    EXPECT_EQ(1, count_contents_bounded(archer->carrying));
}

/* A follower whose quiver is empty is out of arrows, and does not shoot. */
TEST_F(RangerShootTest, AFollowerWithAnEmptyQuiverIsOutOfArrows)
{
    char_data* archer = make_archer();
    char_data* target = make_mob("target", 0);
    equip_char(archer, make_quiver(), WEAR_BACK);

    order_shot(archer);

    EXPECT_TRUE(output_has(OUT_OF_ARROWS_MESSAGE));
    EXPECT_EQ(nullptr, target->carrying);
    EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(target));
}

/* A follower wearing a pack that is not a quiver shoots the arrow in its inventory, not what the
 * pack holds. */
TEST_F(RangerShootTest, AFollowerWearingAPackShootsTheArrowInItsInventory)
{
    char_data* archer = make_archer();
    char_data* target = make_mob("target", 0);
    obj_data* pack = make_object(BAG_VNUM, 0);
    obj_data* bread = make_object(BREAD_VNUM, 0);
    obj_to_obj(bread, pack);
    equip_char(archer, pack, WEAR_BACK);
    obj_data* arrow = make_arrow();
    obj_to_char(arrow, archer);
    push_test_random_value(SURE_HIT_ROLL);

    order_shot(archer);

    EXPECT_EQ(target, arrow->carried_by);
    EXPECT_EQ(1, times_listed(target->carrying, arrow));
    EXPECT_EQ(0, count_contents_bounded(archer->carrying));
    EXPECT_EQ(pack, bread->in_obj) << "what the pack holds stays in it";
    EXPECT_EQ(bread, pack->contains);
}
