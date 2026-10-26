/* Object trigger scripts that remove their own object from the game (EXTRACT_OBJ ob1).
 *
 * Once the script has extracted it, the object is freed, so the command that fired the trigger
 * must not touch it again: examine, wear, wield and pull stop there, eat and drink keep what they
 * had already done, and the enter trigger moves on to the room's other objects. A script that
 * leaves its object alone changes nothing. Later tests check call_object_trigger's results
 * directly, and what a running object script keeps: its variables, when its own trigger runs
 * again inside it or a nested script frees an object it names, and its place in the room. */
#include "scripted_room_fixture.h"

void do_eat(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_drink(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_examine(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_pull(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_wear(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_wield(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);

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

/* ---- eat and taste (ON_EAT) ---- */

TEST_F(ObjectTriggerTest, EatingFoodWhoseScriptExtractsItFeedsThePlayerOnce)
{
    install_self_extracting_script(ON_EAT);
    obj_to_char(make_scripted(BREAD_VNUM), &m_player);

    run_command(do_eat, "bread", SCMD_EAT);

    EXPECT_TRUE(output_has("You eat a loaf of bread."));
    EXPECT_TRUE(output_has(CRUMBLE_MESSAGE));
    EXPECT_EQ(BREAD_FILLING, GET_COND(&m_player, FULL));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, object_list) << "the bread was extracted once and nothing else";
}

TEST_F(ObjectTriggerTest, TastingFoodWhoseScriptExtractsItStopsThere)
{
    install_self_extracting_script(ON_EAT);
    obj_to_char(make_scripted(BREAD_VNUM), &m_player);

    run_command(do_eat, "bread", SCMD_TASTE);

    EXPECT_TRUE(output_has("You nibble a little bit of a loaf of bread."));
    EXPECT_FALSE(output_has("There's nothing left now."));
    EXPECT_EQ(nullptr, m_player.carrying);
}

TEST_F(ObjectTriggerTest, EatingFoodWithAScriptThatKeepsItStillUsesItUp)
{
    install_harmless_script(ON_EAT);
    obj_to_char(make_scripted(BREAD_VNUM), &m_player);

    run_command(do_eat, "bread", SCMD_EAT);

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(BREAD_FILLING, GET_COND(&m_player, FULL));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, object_list);
}

/* Eat ignores what the script returns: food whose script returns false is eaten all the same. */
TEST_F(ObjectTriggerTest, EatingFoodWhoseScriptReturnsFalseStillUsesItUp)
{
    install_refusing_script(ON_EAT);
    obj_to_char(make_scripted(BREAD_VNUM), &m_player);

    run_command(do_eat, "bread", SCMD_EAT);

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, object_list);
}

TEST_F(ObjectTriggerTest, TastingFoodWithAScriptThatKeepsItTakesOneBite)
{
    install_harmless_script(ON_EAT);
    obj_data* bread = make_scripted(BREAD_VNUM);
    obj_to_char(bread, &m_player);

    run_command(do_eat, "bread", SCMD_TASTE);

    EXPECT_EQ(BREAD_FILLING - 1, bread->obj_flags.value[0]);
    EXPECT_EQ(bread, m_player.carrying);
}

/* ---- drink (ON_DRINK) ---- */

TEST_F(ObjectTriggerTest, DrinkingFromAFlaskWhoseScriptExtractsItStopsThere)
{
    install_self_extracting_script(ON_DRINK);
    obj_to_char(make_scripted(FLASK_VNUM), &m_player);

    run_command(do_drink, "flask", SCMD_DRINK);

    EXPECT_TRUE(output_has("You drink the water."));
    EXPECT_TRUE(output_has(CRUMBLE_MESSAGE));
    EXPECT_EQ(nullptr, m_player.carrying);
}

TEST_F(ObjectTriggerTest, DrinkingFromAFlaskWithAScriptThatKeepsItEmptiesItAsUsual)
{
    install_harmless_script(ON_DRINK);
    obj_data* flask = make_scripted(FLASK_VNUM);
    obj_to_char(flask, &m_player);

    run_command(do_drink, "flask", SCMD_SIP);

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(FLASK_DRINKS - 1, flask->obj_flags.value[1]) << "a sip takes one drink";
}

/* ---- wear and wield (ON_WEAR) ---- */

TEST_F(ObjectTriggerTest, WearingARingWhoseScriptExtractsItStopsThere)
{
    install_self_extracting_script(ON_WEAR);
    obj_to_char(make_scripted(RING_VNUM), &m_player);

    run_command(do_wear, "ring");

    EXPECT_TRUE(output_has(CRUMBLE_MESSAGE));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, m_player.equipment[WEAR_FINGER_R]);
    EXPECT_EQ(nullptr, m_player.equipment[WEAR_FINGER_L]);
}

TEST_F(ObjectTriggerTest, WieldingASwordWhoseScriptExtractsItStopsThere)
{
    install_self_extracting_script(ON_WEAR);
    obj_to_char(make_scripted(SWORD_VNUM), &m_player);

    run_command(do_wield, "sword");

    EXPECT_TRUE(output_has(CRUMBLE_MESSAGE));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, m_player.equipment[WIELD]);
}

TEST_F(ObjectTriggerTest, WearingARingWithAScriptThatKeepsItWearsIt)
{
    install_harmless_script(ON_WEAR);
    obj_data* ring = make_scripted(RING_VNUM);
    obj_to_char(ring, &m_player);

    run_command(do_wear, "ring");

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(ring, m_player.equipment[WEAR_FINGER_R]);
}

TEST_F(ObjectTriggerTest, WearingARingWhoseScriptReturnsFalseLeavesItCarried)
{
    install_refusing_script(ON_WEAR);
    obj_data* ring = make_scripted(RING_VNUM);
    obj_to_char(ring, &m_player);

    run_command(do_wear, "ring");

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(ring, m_player.carrying);
    EXPECT_EQ(nullptr, m_player.equipment[WEAR_FINGER_R]);
}

/* ---- pull (ON_PULL) ---- */

TEST_F(ObjectTriggerTest, PullingALeverWhoseScriptExtractsItStopsThere)
{
    install_self_extracting_script(ON_PULL);
    obj_data* lever = make_scripted(LEVER_VNUM);
    obj_to_room(lever, 0);

    pull(lever);

    EXPECT_TRUE(output_has(CRUMBLE_MESSAGE));
    EXPECT_FALSE(output_has("seems to be broken"));
    EXPECT_EQ(nullptr, world[0].contents);
}

/* The lever's exit is not set, so a pull that goes ahead finds it broken. */
TEST_F(ObjectTriggerTest, PullingALeverWithAScriptThatKeepsItGoesAhead)
{
    install_harmless_script(ON_PULL);
    obj_data* lever = make_scripted(LEVER_VNUM);
    obj_to_room(lever, 0);

    pull(lever);

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_TRUE(output_has("iron lever seems to be broken."));
    EXPECT_EQ(lever, world[0].contents);
}

/* ---- entering a room (ON_ENTER on the room's objects) ---- */

TEST_F(ObjectTriggerTest, EnterTriggersEveryObjectWhenEachExtractsItself)
{
    install_self_extracting_script(ON_ENTER);
    obj_to_room(make_scripted(STONE_VNUM), 0);
    obj_to_room(make_scripted(STONE_VNUM), 0);
    obj_to_room(make_scripted(STONE_VNUM), 0);

    call_trigger(ON_ENTER, &world[0], &m_player, 0);

    EXPECT_EQ(3, count_in_output(CRUMBLE_MESSAGE));
    EXPECT_EQ(nullptr, world[0].contents);
}

TEST_F(ObjectTriggerTest, EnterTriggersEveryObjectWhoseScriptKeepsIt)
{
    install_harmless_script(ON_ENTER);
    obj_to_room(make_scripted(STONE_VNUM), 0);
    obj_to_room(make_scripted(STONE_VNUM), 0);

    call_trigger(ON_ENTER, &world[0], &m_player, 0);

    EXPECT_EQ(2, count_in_output(SCRIPT_RAN_MESSAGE));
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

/* An object's own triggers run only through call_object_trigger. */
TEST_F(ObjectTriggerTest, CallTriggerRunsNoObjectTrigger)
{
    install_self_extracting_script(ON_WEAR);
    obj_data* ring = make_scripted(RING_VNUM);

    EXPECT_EQ(1, call_trigger(ON_WEAR, ring, &m_player, 0));
    EXPECT_FALSE(output_has(CRUMBLE_MESSAGE));
    EXPECT_EQ(ring, object_list);
}

/* ---- what a running object script keeps ---- */

/* The stone's script runs on after it extracted the stone, with its variables intact. */
TEST_F(ObjectTriggerTest, AScriptRunsOnAfterItExtractsItsObject)
{
    install_script(ON_EXAMINE_OBJECT,
        { { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB1 } },
            { SCRIPT_SEND_TO_CHAR, "Still running.", { SCRIPT_PARAM_CH1 } } });
    obj_data* stone = make_scripted(STONE_VNUM);
    obj_to_char(stone, &m_player);

    EXPECT_EQ(object_trigger_result::object_extracted,
        call_object_trigger(ON_EXAMINE_OBJECT, stone, &m_player));
    EXPECT_TRUE(output_has("Still running."));
}

/* Examining the ring wears it, and the ring's wear script extracts it while the examine script
 * still holds it in a second variable: that variable is emptied too. */
TEST_F(ObjectTriggerTest, AnObjectFreedByANestedScriptIsGoneFromEveryVariable)
{
    install_scripts({ { SCRIPT_VNUM,
        { { ON_EXAMINE_OBJECT,
              { { SCRIPT_ASSIGN_INV, nullptr,
                    { RING_VNUM, SCRIPT_PARAM_OB3, SCRIPT_PARAM_CH1, SCRIPT_PARAM_INT1 } },
                  { SCRIPT_DO_WEAR, nullptr, { SCRIPT_PARAM_CH1, SCRIPT_PARAM_OB1 } },
                  { SCRIPT_SEND_TO_CHAR, "It was [%s].", { SCRIPT_PARAM_CH1, SCRIPT_PARAM_OB3_NAME } } } },
            { ON_WEAR, { { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB1 } } } } } } });
    obj_data* ring = make_scripted(RING_VNUM);
    obj_to_char(ring, &m_player);

    EXPECT_EQ(object_trigger_result::object_extracted,
        call_object_trigger(ON_EXAMINE_OBJECT, ring, &m_player));
    EXPECT_TRUE(output_has("It was []."));
}

/* Examining the ring wears it, which runs the ring's own wear script inside the examine script;
 * the examine script still has its variables when it carries on. */
TEST_F(ObjectTriggerTest, AScriptKeepsItsVariablesWhenItSetsOffItsObjectsTriggerAgain)
{
    install_scripts({ { SCRIPT_VNUM,
        { { ON_EXAMINE_OBJECT,
              { { SCRIPT_ASSIGN_INV, nullptr,
                    { STONE_VNUM, SCRIPT_PARAM_OB2, SCRIPT_PARAM_CH1, SCRIPT_PARAM_INT1 } },
                  { SCRIPT_DO_WEAR, nullptr, { SCRIPT_PARAM_CH1, SCRIPT_PARAM_OB1 } },
                  { SCRIPT_SEND_TO_CHAR, "Holding %s.", { SCRIPT_PARAM_CH1, SCRIPT_PARAM_OB2_NAME } } } },
            { ON_WEAR, { { SCRIPT_SEND_TO_CHAR, "The ring glows.", { SCRIPT_PARAM_CH1 } } } } } } });
    obj_to_char(make_object(STONE_VNUM, 0), &m_player);
    obj_data* ring = make_scripted(RING_VNUM);
    obj_to_char(ring, &m_player);

    EXPECT_EQ(object_trigger_result::proceed,
        call_object_trigger(ON_EXAMINE_OBJECT, ring, &m_player));

    EXPECT_TRUE(output_has("The ring glows."));
    EXPECT_EQ(ring, m_player.equipment[WEAR_FINGER_R]);
    EXPECT_TRUE(output_has("Holding a grey stone.")) << output();
}

/* The stone's enter script moves it from the room into the player's hands: the enter trigger
 * goes on with the room's next object, not with what the player carries. */
TEST_F(ObjectTriggerTest, EnterGoesOnInTheRoomAfterAnObjectMovesItselfOut)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_ENTER,
                              { { SCRIPT_OBJ_FROM_ROOM, nullptr, { SCRIPT_PARAM_OB1, SCRIPT_PARAM_CH1_ROOM } },
                                  { SCRIPT_OBJ_TO_CHAR, nullptr, { SCRIPT_PARAM_OB1, SCRIPT_PARAM_CH1 } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_ENTER, { { SCRIPT_SEND_TO_CHAR, "[%s] stirs.", { SCRIPT_PARAM_CH1, SCRIPT_PARAM_OB1_NAME } } } } } } });
    obj_to_char(make_object(BREAD_VNUM, OTHER_SCRIPT_VNUM), &m_player);
    obj_to_room(make_object(BAG_VNUM, OTHER_SCRIPT_VNUM), 0);
    obj_data* stone = make_scripted(STONE_VNUM);
    obj_to_room(stone, 0); /* first in the room */

    call_trigger(ON_ENTER, &world[0], &m_player, 0);

    EXPECT_EQ(&m_player, stone->carried_by);
    EXPECT_TRUE(output_has("[a cloth bag] stirs.")) << output();
    EXPECT_FALSE(output_has("[a loaf of bread] stirs.")) << output();
}

/* A copy never shares its prototype's script state, so freeing it cannot free the prototype's. */
TEST_F(ObjectTriggerTest, AnObjectIsMadeWithoutItsPrototypesScriptState)
{
    info_script prototype_state {};
    prototype_for(STONE_VNUM).obj_flags.script_info = &prototype_state;
    obj_data* stone = make_object(STONE_VNUM, 0);
    prototype_for(STONE_VNUM).obj_flags.script_info = nullptr;

    EXPECT_EQ(nullptr, stone->obj_flags.script_info);
}
