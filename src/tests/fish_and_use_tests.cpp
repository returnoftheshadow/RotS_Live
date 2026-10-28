/* Fishing, and using a wand or a staff: what each leaves in the world, and what a builder's
 * mistake in a wand or staff does. */
#include "scripted_room_fixture.h"

#include "../spells.h"

ACMD(do_fish);
ACMD(do_use);

using namespace scripted_room;

namespace {

/* The fish do_fish hands over on a catch. */
constexpr int FISH_VNUM = 7230;
/* The test prototype that stands for the fish: the last one, so that the vnums stay sorted. */
constexpr int FISH_PROTOTYPE = PROTOTYPE_COUNT - 1;
/* The fishing stages do_fish is called with. */
constexpr int FISH_CANCEL = -1;
constexpr int FISH_CAST = 0;
constexpr int FISH_PULL_IN = 1;
/* A spell number past the end of the skills table, and one before its start. */
constexpr int SPELL_PAST_THE_TABLE = MAX_SKILLS + 44;
constexpr int SPELL_BEFORE_THE_TABLE = -5;

class FishAndUseTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        link_second_room();
        m_saved_sector = world[0].sector_type;
        for (int room = 0; room <= SECOND_ROOM; ++room) {
            m_saved_room_flags[room] = world[room].room_flags;
        }
        obj_index[FISH_PROTOTYPE].virt = FISH_VNUM;
        m_player.tmpabilities.move = 100;
    }

    void TearDown() override
    {
        world[0].sector_type = m_saved_sector;
        for (int room = 0; room <= SECOND_ROOM; ++room) {
            world[room].room_flags = m_saved_room_flags[room];
        }
        while (m_player.affected) {
            affect_remove(&m_player, m_player.affected);
        }
        ScriptedRoomFixture::TearDown();
    }

    /* Puts the player by deep water with a fishing pole in hand. */
    void ready_to_fish()
    {
        world[0].sector_type = SECT_WATER_NOSWIM;
        obj_data* pole = make_object(SWORD_VNUM, 0);
        pole->name = m_pole_keywords;
        equip_char(&m_player, pole, HOLD);
        ASSERT_EQ(pole, m_player.equipment[HOLD]);
    }

    /* Puts in the player's hand a wand or staff (<item_type>) of <spell_number> with one charge. */
    obj_data* hold_stick(int item_type, int spell_number)
    {
        obj_data* stick = make_object(RING_VNUM, 0);
        stick->obj_flags.type_flag = item_type;
        stick->obj_flags.value[2] = 1; /* charges */
        stick->obj_flags.value[3] = spell_number;
        equip_char(&m_player, stick, HOLD);
        EXPECT_EQ(stick, m_player.equipment[HOLD]);
        return stick;
    }

    /* Empties what the player has been sent. */
    void clear_output()
    {
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufptr = 0;
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
    }

    /* How many objects are in the game. */
    static int count_objects()
    {
        int count = 0;
        for (const obj_data* object = object_list; object; object = object->next) {
            ++count;
        }
        return count;
    }

    /* How many copies of the fish's prototype the game counts. */
    static int fish_in_the_world() { return obj_index[FISH_PROTOTYPE].number; }

private:
    // What SetUp found as room 0's terrain and rooms 0 and SECOND_ROOM's flags, put back by
    // TearDown.
    int m_saved_sector = 0;
    long m_saved_room_flags[SECOND_ROOM + 1] {};
    // The pole's keywords; do_fish looks for "fishing".
    char m_pole_keywords[24] = "pole fishing";
};

} // namespace
