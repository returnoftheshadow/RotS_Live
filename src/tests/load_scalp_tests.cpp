#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"
#include <cstdlib>
#include <cstring>
#include <gtest/gtest.h>

extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;
extern struct index_data* obj_index;
extern int top_of_objt;
extern struct obj_data* object_list;
extern int generic_scalp;
extern struct room_data world;
extern int top_of_world;
obj_data* load_scalp(int number);
void clear_char(struct char_data* ch, int mode);
void clear_object(struct obj_data* obj);
ACMD(do_butcher);

namespace {

const int kWargVnum = 5000;
const int kRemovedMobVnum = 30243; // Keeler's scalp in live data names this mob; it is gone.
const int CORPSE_VNUM = 1200; // a corpse prototype, after the scalp in the vnum-sorted index
const int CORPSE_RNUM = 1;
const int BLUDGEON_WEAPON_TYPE = 7; // weapon_hit_type gives TYPE_BLUDGEON: the head is destroyed

// One mob prototype (a warg) and the scalp object prototype. mob_proto points ONE element into a
// two-element buffer, and the element before the table is a bodyless mob: that is what
// mob_proto[-1] reads when load_scalp indexes the table with real_mobile's -1, so the unguarded read
// resolves the same way on every run instead of whatever happens to precede the real table.
class LoadScalpTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_mob_proto = mob_proto;
        m_saved_mob_index = mob_index;
        m_saved_top_of_mobt = top_of_mobt;
        m_saved_obj_index = obj_index;
        m_saved_top_of_objt = top_of_objt;
        m_saved_object_list = object_list;

        m_mob_storage[0].player.bodytype = 0; // the neighbour just before the table
        m_mob_storage[1].player.bodytype = 1;
        m_mob_storage[1].player.short_descr = m_warg_short;
        mob_proto = &m_mob_storage[1];
        m_mob_index[0].virt = kWargVnum;
        mob_index = m_mob_index;
        top_of_mobt = 0;

        m_obj_index[0].virt = generic_scalp;
        m_obj_index[CORPSE_RNUM].virt = CORPSE_VNUM;
        obj_index = m_obj_index;
        top_of_objt = CORPSE_RNUM;
        object_list = nullptr;
    }

    void TearDown() override
    {
        while (object_list != nullptr) {
            obj_data* next = object_list->next;
            std::free(object_list->name);
            std::free(object_list->description);
            std::free(object_list->short_description);
            std::free(object_list);
            object_list = next;
        }
        mob_proto = m_saved_mob_proto;
        mob_index = m_saved_mob_index;
        top_of_mobt = m_saved_top_of_mobt;
        obj_index = m_saved_obj_index;
        top_of_objt = m_saved_top_of_objt;
        object_list = m_saved_object_list;
    }

    char m_warg_short[16] = "a warg";
    char_data m_mob_storage[2] {};
    index_data m_mob_index[1] {};
    index_data m_obj_index[2] {};

private:
    char_data* m_saved_mob_proto = nullptr;
    index_data* m_saved_mob_index = nullptr;
    int m_saved_top_of_mobt = 0;
    index_data* m_saved_obj_index = nullptr;
    int m_saved_top_of_objt = 0;
    obj_data* m_saved_object_list = nullptr;
};

TEST_F(LoadScalpTest, ScalpOfARemovedMobLoadsAsAnOldSkull)
{
    obj_data* scalp = load_scalp(kRemovedMobVnum);

    ASSERT_NE(scalp, nullptr) << "a scalp whose mob no longer exists must not be destroyed at login";
    EXPECT_STREQ(scalp->short_description, "An old skull");
    EXPECT_EQ(scalp->obj_flags.value[4], kRemovedMobVnum) << "the original head number is kept";
}

TEST_F(LoadScalpTest, ScalpOfAnExistingMobNamesTheMob)
{
    obj_data* scalp = load_scalp(kWargVnum);

    ASSERT_NE(scalp, nullptr);
    EXPECT_STREQ(scalp->short_description, "A severed head of a warg");
}

TEST_F(LoadScalpTest, BodylessMobStillYieldsNoScalp)
{
    m_mob_storage[1].player.bodytype = 0;

    EXPECT_EQ(load_scalp(kWargVnum), nullptr);
}

/* A scalp's name and descriptions are its own: extracting the scalp frees them. */
TEST_F(LoadScalpTest, AScalpOwnsTheTextItIsMadeWith)
{
    obj_data* scalp = load_scalp(kWargVnum);

    ASSERT_NE(scalp, nullptr);
    ASSERT_GE(scalp->item_number, 0) << "the scalp prototype is in the table";
    EXPECT_TRUE(scalp->owns_text(object_text::name));
    EXPECT_TRUE(scalp->owns_text(object_text::short_description));
    EXPECT_TRUE(scalp->owns_text(object_text::description));
    EXPECT_FALSE(scalp->owns_text(object_text::action_description));

    extract_obj(scalp);
    EXPECT_EQ(object_list, nullptr);
}

/* A scalp counts as a copy of the scalp prototype while it is in the game, as any copy does. */
TEST_F(LoadScalpTest, AScalpCountsAsACopyOfItsPrototypeUntilExtracted)
{
    const int count_before = m_obj_index[0].number;

    obj_data* scalp = load_scalp(kWargVnum);

    ASSERT_NE(scalp, nullptr);
    ASSERT_EQ(scalp->item_number, 0) << "the scalp prototype is the first in the table";
    EXPECT_EQ(m_obj_index[0].number, count_before + 1);

    extract_obj(scalp);
    EXPECT_EQ(m_obj_index[0].number, count_before);
}

/* Two copies of a corpse prototype on the floor of room 0, sharing its text the way copies do,
 * and a character there holding a club. */
class BeheadCorpseTest : public LoadScalpTest {
protected:
    void SetUp() override
    {
        LoadScalpTest::SetUp();
        if (room_data::BASE_WORLD == nullptr) {
            world.create_bulk(1);
        }
        m_saved_top_of_world = top_of_world;
        m_saved_contents = world[0].contents;
        top_of_world = 0;
        world[0].contents = nullptr;

        m_corpse_name = str_dup("corpse");
        m_corpse_description = str_dup("The corpse of a warg is lying here.");
        m_untouched = make_corpse();
        m_beheaded = make_corpse(); /* first on the floor, so "corpse" finds it */

        clear_object(&m_club);
        m_club.obj_flags.type_flag = ITEM_WEAPON;
        m_club.obj_flags.value[3] = BLUDGEON_WEAPON_TYPE;

        clear_char(&m_character, MOB_VOID);
        m_character.in_room = 0;
        SET_BIT(PRF_FLAGS(&m_character), PRF_HOLYLIGHT); /* sees in the unlit test room */
        m_character.equipment[WIELD] = &m_club;
    }

    void TearDown() override
    {
        m_character.equipment[WIELD] = nullptr;
        RELEASE(m_character.profs);
        RELEASE(m_character.skills);
        RELEASE(m_character.knowledge);
        while (world[0].contents != nullptr) {
            extract_obj(world[0].contents);
        }
        RELEASE(m_corpse_name);
        RELEASE(m_corpse_description);
        world[0].contents = m_saved_contents;
        top_of_world = m_saved_top_of_world;
        LoadScalpTest::TearDown();
    }

    obj_data* make_corpse()
    {
        obj_data* corpse = nullptr;
        CREATE(corpse, obj_data, 1);
        clear_object(corpse);
        corpse->item_number = CORPSE_RNUM;
        corpse->name = m_corpse_name;
        corpse->description = m_corpse_description;
        corpse->obj_flags.type_flag = ITEM_CONTAINER;
        corpse->obj_flags.value[3] = 1; /* a corpse */
        corpse->obj_flags.value[4] = kWargVnum; /* whose head it still has */
        corpse->in_room = 0;
        corpse->next_content = world[0].contents;
        world[0].contents = corpse;
        return corpse;
    }

    char* m_corpse_name = nullptr; // the corpse prototype's name, shared by both copies
    char* m_corpse_description = nullptr; // the prototype's room line, shared by both copies
    obj_data* m_untouched = nullptr; // the copy left alone
    obj_data* m_beheaded = nullptr; // the copy the character beheads
    obj_data m_club {}; // the character's wielded bludgeon
    char_data m_character {}; // the one doing the beheading
    int m_saved_top_of_world = 0; // restored in TearDown
    obj_data* m_saved_contents = nullptr; // room 0's floor before the test, restored in TearDown
};

/* Beheading one corpse gives it a room line of its own and leaves the line the other copy shares,
 * full stop included, as it was. */
TEST_F(BeheadCorpseTest, BeheadingACopyLeavesTheSharedRoomLineAlone)
{
    char argument[] = "corpse";
    do_butcher(&m_character, argument, nullptr, 0, SCMD_SCALP);

    EXPECT_STREQ(m_beheaded->description, "The corpse of a warg is lying here, its head a bloody mess.");
    EXPECT_TRUE(m_beheaded->owns_text(object_text::description));
    EXPECT_EQ(m_untouched->description, m_corpse_description);
    EXPECT_STREQ(m_untouched->description, "The corpse of a warg is lying here.");
    EXPECT_FALSE(m_untouched->owns_text(object_text::description));
}

} // namespace
