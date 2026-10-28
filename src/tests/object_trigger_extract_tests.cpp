/* Object trigger scripts that remove their own object from the game (EXTRACT_OBJ ob1).
 *
 * Once the script has extracted it, the object is freed, so the command that fired the trigger
 * must not touch it again: examine, wear, wield and pull stop there, and eat and drink keep what
 * they had already done. A script that leaves its object alone changes nothing. Later tests check
 * call_object_trigger's results directly. */
#include "scripted_room_fixture.h"

ACMD(do_eat);
ACMD(do_drink);
ACMD(do_examine);
ACMD(do_pull);
ACMD(do_wear);
ACMD(do_wield);

using namespace scripted_room;

namespace {

class ObjectTriggerTest : public ScriptedRoomFixture {
protected:
    /* Pulls this object, as the interpreter does once it has found it. */
    void pull(obj_data* object)
    {
        waiting_type target {};
        target.targ1.type = TARGET_OBJ;
        target.targ1.ptr.obj = object;
        char empty_argument[1] = "";
        do_pull(&m_player, empty_argument, &target, 0, 0);
    }
};

} // namespace

/* ---- examine (ON_EXAMINE_OBJECT) ---- */

TEST_F(ObjectTriggerTest, ExamineStopsWhenTheScriptExtractsTheObject)
{
    install_self_extracting_script(ON_EXAMINE_OBJECT);
    obj_to_char(make_scripted(STONE_VNUM), &m_player);

    run_command(do_examine, "stone");

    EXPECT_TRUE(output_has(CRUMBLE_MESSAGE));
    EXPECT_FALSE(output_has("You see nothing special")) << "the description step read the stone";
    EXPECT_EQ(nullptr, m_player.carrying);
}

TEST_F(ObjectTriggerTest, ExamineShowsTheDescriptionAfterAScriptThatKeepsTheObject)
{
    install_harmless_script(ON_EXAMINE_OBJECT);
    obj_data* stone = make_scripted(STONE_VNUM);
    obj_to_char(stone, &m_player);

    run_command(do_examine, "stone");

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_TRUE(output_has("You see nothing special"));
    EXPECT_EQ(stone, m_player.carrying);
}

TEST_F(ObjectTriggerTest, ExamineShowsNoDescriptionWhenTheScriptReturnsFalse)
{
    install_refusing_script(ON_EXAMINE_OBJECT);
    obj_to_char(make_scripted(STONE_VNUM), &m_player);

    run_command(do_examine, "stone");

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_FALSE(output_has("You see nothing special"));
}

/* ---- call_object_trigger and call_trigger ---- */

TEST_F(ObjectTriggerTest, ScriptThatKeepsItsObjectProceeds)
{
    install_harmless_script(ON_EXAMINE_OBJECT);
    obj_data* stone = make_scripted(STONE_VNUM);

    EXPECT_EQ(object_trigger_result::proceed,
        call_object_trigger(ON_EXAMINE_OBJECT, stone, &m_player));
    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
}

TEST_F(ObjectTriggerTest, ScriptThatReturnsFalseIsRefused)
{
    install_refusing_script(ON_EXAMINE_OBJECT);
    obj_data* stone = make_scripted(STONE_VNUM);

    EXPECT_EQ(object_trigger_result::refused,
        call_object_trigger(ON_EXAMINE_OBJECT, stone, &m_player));
}

/* The script extracts the bag the stone is in, which frees the stone with it. */
TEST_F(ObjectTriggerTest, ObjectFreedWithItsContainerIsReportedExtracted)
{
    install_script(ON_EXAMINE_OBJECT,
        { { SCRIPT_ASSIGN_INV, nullptr,
              { BAG_VNUM, SCRIPT_PARAM_OB2, SCRIPT_PARAM_CH1, SCRIPT_PARAM_INT1 } },
            { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB2 } } });
    obj_data* bag = make_scripted(BAG_VNUM);
    bag->obj_flags.script_number = 0;
    obj_data* stone = make_scripted(STONE_VNUM);
    obj_to_obj(stone, bag);
    obj_to_char(bag, &m_player);

    EXPECT_EQ(object_trigger_result::object_extracted,
        call_object_trigger(ON_EXAMINE_OBJECT, stone, &m_player));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, object_list);
}

TEST_F(ObjectTriggerTest, ObjectWithoutTheTriggersScriptProceeds)
{
    install_self_extracting_script(ON_EAT);
    obj_data* stone = make_scripted(STONE_VNUM);

    EXPECT_EQ(object_trigger_result::proceed,
        call_object_trigger(ON_EXAMINE_OBJECT, stone, &m_player));
    EXPECT_EQ(stone, object_list);
}

/* ---- an object script that removes the mob using the object ---- */

/* The socket of the examiner's connection: any but the player's (1). */
constexpr int EXAMINER_SOCKET = 2;

/* A mob examines a stone whose script removes the mob: examine stops touching it. Only a
 * character with a connection looks, so the mob is one an immortal has switched into. */
TEST_F(ObjectTriggerTest, ExamineStopsWhenTheScriptRemovesTheExaminer)
{
    install_script(ON_EXAMINE_OBJECT, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* examiner = make_mob("examiner", 0);
    descriptor_data connection {};
    connection.output = connection.small_outbuf;
    connection.bufspace = SMALL_BUFSIZE - 1;
    connection.connected = CON_PLYNG;
    connection.descriptor = EXAMINER_SOCKET;
    connection.character = examiner;
    examiner->desc = &connection;
    obj_to_char(make_scripted(STONE_VNUM), examiner);

    run_command_as(examiner, do_examine, "stone");

    EXPECT_FALSE(is_in_game(examiner));
    EXPECT_EQ(1, count_contents(world[0].contents)) << "the examiner dropped the stone";
}
