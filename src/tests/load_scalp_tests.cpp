#include "../db.h"
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
obj_data* load_scalp(int number);

namespace {

const int kWargVnum = 5000;
const int kRemovedMobVnum = 30243; // Keeler's scalp in live data names this mob; it is gone.

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
        obj_index = m_obj_index;
        top_of_objt = 0;
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
    index_data m_obj_index[1] {};

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

} // namespace
