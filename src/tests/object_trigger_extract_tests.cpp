/* Object trigger scripts that remove their own object from the game (EXTRACT_OBJ ob1).
 *
 * Once the script has extracted it, the object is freed, so the command that fired the trigger
 * must not touch it again: examine, wear, wield and pull stop there, and eat and drink keep what
 * they had already done. A script that leaves its object alone changes nothing. Later tests check
 * call_object_trigger's results directly. */
#include "scripted_room_fixture.h"

#include <utility>

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

/* ---- eat and taste (ON_EAT) ---- */

/* The bites a loaf has that is one taste from gone. */
constexpr int LAST_BITE = 1;

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

/* The script extracts the food on its last bite: the taste stops there, with nothing to say about
 * a bite left or not. */
TEST_F(ObjectTriggerTest, TastingFoodWhoseScriptExtractsItStopsThere)
{
    install_self_extracting_script(ON_EAT);
    prototype_for(BREAD_VNUM).obj_flags.value[0] = LAST_BITE;
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

TEST_F(ObjectTriggerTest, TastingTheLastBiteWithAScriptThatKeepsItFinishesTheFood)
{
    install_harmless_script(ON_EAT);
    prototype_for(BREAD_VNUM).obj_flags.value[0] = LAST_BITE;
    obj_to_char(make_scripted(BREAD_VNUM), &m_player);

    run_command(do_eat, "bread", SCMD_TASTE);

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_TRUE(output_has("There's nothing left now."));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, object_list);
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

/* Drink ignores what the script returns: a sip from a flask whose script returns false is taken
 * from it all the same. */
TEST_F(ObjectTriggerTest, DrinkingFromAFlaskWhoseScriptReturnsFalseEmptiesItAsUsual)
{
    install_refusing_script(ON_DRINK);
    obj_data* flask = make_scripted(FLASK_VNUM);
    obj_to_char(flask, &m_player);

    run_command(do_drink, "flask", SCMD_SIP);

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(FLASK_DRINKS - 1, flask->obj_flags.value[1]);
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

/* A weapon at least this bulky is wielded in two hands. */
constexpr int TWO_HANDED_BULK = 5;

TEST_F(ObjectTriggerTest, WieldingABulkySwordWhoseScriptKeepsItTakesBothHands)
{
    install_harmless_script(ON_WEAR);
    prototype_for(SWORD_VNUM).obj_flags.value[2] = TWO_HANDED_BULK;
    obj_data* sword = make_scripted(SWORD_VNUM);
    obj_to_char(sword, &m_player);

    run_command(do_wield, "sword");

    EXPECT_EQ(sword, m_player.equipment[WIELD]);
    EXPECT_TRUE(IS_TWOHANDED(&m_player));
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

/* The first stone's script returns false: the stones after it do not react. */
TEST_F(ObjectTriggerTest, AnObjectThatRefusesStopsTheObjectsAfterItReacting)
{
    install_scripts({ { SCRIPT_VNUM, { { ON_ENTER, { { SCRIPT_RETURN_FALSE, nullptr, {} } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_ENTER, { { SCRIPT_SEND_TO_CHAR, SCRIPT_RAN_MESSAGE, { SCRIPT_PARAM_CH1 } } } } } } });
    obj_to_room(make_object(STONE_VNUM, OTHER_SCRIPT_VNUM), 0);
    obj_to_room(make_scripted(STONE_VNUM), 0); /* first in the room */

    EXPECT_EQ(0, call_trigger(ON_ENTER, &world[0], &m_player, 0));

    EXPECT_FALSE(output_has(SCRIPT_RAN_MESSAGE));
}

/* The first stone's script removes the mob walking in: the stone after it still reacts, told
 * nobody is entering, so it has nobody's room to tell. */
TEST_F(ObjectTriggerTest, ALaterObjectStillReactsWhenAnObjectsScriptRemovesTheOneEntering)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_ENTER, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_ENTER,
                { { SCRIPT_SEND_TO_ROOM, "Someone is there.", { SCRIPT_PARAM_CH1_ROOM } },
                    { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB1 } } } } } } });
    obj_data* later_stone = make_object(STONE_VNUM, OTHER_SCRIPT_VNUM);
    obj_to_room(later_stone, 0);
    obj_to_room(make_scripted(STONE_VNUM), 0); /* first in the room */
    char_data* wanderer = make_mob("wanderer", 0);

    EXPECT_EQ(1, call_trigger(ON_ENTER, &world[0], wanderer, 0));

    EXPECT_FALSE(is_in_game(wanderer));
    EXPECT_FALSE(is_in_object_list(later_stone)) << "its script ran";
    EXPECT_FALSE(output_has("Someone is there."));
}

/* The stone's enter script moves it from the room into the player's hands: the enter trigger goes
 * on from the stone, as it always has, along the list the stone is now in, the player's. */
TEST_F(ObjectTriggerTest, EnterFollowsAnObjectThatMovesItselfOut)
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
    EXPECT_TRUE(output_has("[a loaf of bread] stirs.")) << output();
    EXPECT_FALSE(output_has("[a cloth bag] stirs.")) << output();
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

/* What the script returned does not matter once its object is gone. */
TEST_F(ObjectTriggerTest, ScriptThatExtractsItsObjectAndReturnsFalseIsReportedExtracted)
{
    install_script(ON_EXAMINE_OBJECT,
        { { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB1 } }, { SCRIPT_RETURN_FALSE, nullptr, {} } });
    obj_data* stone = make_scripted(STONE_VNUM);

    EXPECT_EQ(object_trigger_result::object_extracted,
        call_object_trigger(ON_EXAMINE_OBJECT, stone, &m_player));
    EXPECT_EQ(nullptr, object_list);
}

/* A null character is allowed: the script runs, and has nobody to tell it ran. */
TEST_F(ObjectTriggerTest, ScriptRunsWithNoCharacter)
{
    install_self_extracting_script(ON_EXAMINE_OBJECT);
    obj_data* stone = make_scripted(STONE_VNUM);

    EXPECT_EQ(object_trigger_result::object_extracted,
        call_object_trigger(ON_EXAMINE_OBJECT, stone, nullptr));
    EXPECT_EQ(nullptr, object_list);
    EXPECT_FALSE(output_has(CRUMBLE_MESSAGE));
}

TEST_F(ObjectTriggerTest, NoObjectIsLoggedAndReportedExtracted)
{
    testing::internal::CaptureStderr();
    const object_trigger_result result = call_object_trigger(ON_EXAMINE_OBJECT, nullptr, &m_player);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(object_trigger_result::object_extracted, result);
    EXPECT_NE(std::string::npos, logged.find("SYSERR: call_object_trigger: trigger")) << logged;
    EXPECT_NE(std::string::npos, logged.find("for no object")) << logged;
}

TEST_F(ObjectTriggerTest, ATriggerThatIsNotAnObjectsOwnRunsNothingIsLoggedAndProceeds)
{
    install_self_extracting_script(ON_ENTER);
    obj_data* stone = make_scripted(STONE_VNUM);

    testing::internal::CaptureStderr();
    const object_trigger_result result = call_object_trigger(ON_ENTER, stone, &m_player);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(object_trigger_result::proceed, result);
    EXPECT_NE(std::string::npos, logged.find("SYSERR: call_object_trigger: trigger")) << logged;
    EXPECT_NE(std::string::npos, logged.find("is not an object's own trigger")) << logged;
    EXPECT_FALSE(output_has(CRUMBLE_MESSAGE));
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

/* Each of the object's own triggers is reported: a call_trigger that names one is a mistake. */
TEST_F(ObjectTriggerTest, CallTriggerLogsAnObjectsOwnTrigger)
{
    obj_data* stone = make_object(STONE_VNUM, 0);

    for (const int trigger_type : { ON_DRINK, ON_EAT, ON_EXAMINE_OBJECT, ON_PULL, ON_WEAR }) {
        testing::internal::CaptureStderr();
        const int result = call_trigger(trigger_type, stone, &m_player, 0);
        const std::string logged = testing::internal::GetCapturedStderr();

        EXPECT_EQ(1, result);
        EXPECT_NE(std::string::npos, logged.find("SYSERR: call_trigger: trigger")) << logged;
        EXPECT_NE(std::string::npos, logged.find("use call_object_trigger")) << logged;
    }
}

/* ---- an object script that removes the mob using the object ---- */

/* The socket of the examiner's connection: any but the player's (1). */
constexpr int EXAMINER_SOCKET = 2;

/* A mob examines a stone whose script removes the mob: examine stops touching it. Only a
 * character with a connection looks, so the mob is given a connection of its own. */
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

/* The taster is gone, but the bite it took is taken all the same. */
TEST_F(ObjectTriggerTest, TastingStillTakesABiteWhenTheScriptRemovesTheTaster)
{
    install_script(ON_EAT, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* taster = make_mob("taster", 0);
    obj_data* bread = make_scripted(BREAD_VNUM);
    obj_to_char(bread, taster);

    run_command_as(taster, do_eat, "bread", SCMD_TASTE);

    EXPECT_FALSE(is_in_game(taster));
    EXPECT_EQ(bread, world[0].contents);
    EXPECT_EQ(BREAD_FILLING - 1, bread->obj_flags.value[0]);
}

/* The last bite is used up too, with nobody left to tell. AddressSanitizer is what protects the
 * telling: the words would go to the freed taster. */
TEST_F(ObjectTriggerTest, TastingTheLastBiteFinishesTheFoodWhenTheScriptRemovesTheTaster)
{
    install_script(ON_EAT, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    prototype_for(BREAD_VNUM).obj_flags.value[0] = LAST_BITE;
    char_data* taster = make_mob("taster", 0);
    obj_to_char(make_scripted(BREAD_VNUM), taster);

    run_command_as(taster, do_eat, "bread", SCMD_TASTE);

    EXPECT_FALSE(is_in_game(taster));
    EXPECT_EQ(nullptr, world[0].contents);
    EXPECT_EQ(nullptr, object_list);
}

/* AddressSanitizer is what protects this test: a pull that went on would read the freed puller,
 * and the puller, a mob with no connection, shows nothing a test could read. */
TEST_F(ObjectTriggerTest, PullingStopsWhenTheScriptRemovesThePuller)
{
    install_script(ON_PULL, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* puller = make_mob("puller", 0);
    obj_data* lever = make_scripted(LEVER_VNUM);
    obj_to_room(lever, 0);
    waiting_type target {};
    target.targ1.type = TARGET_OBJ;
    target.targ1.ptr.obj = lever;
    char empty_argument[1] = "";

    do_pull(puller, empty_argument, &target, 0, 0);

    EXPECT_FALSE(is_in_game(puller));
}

/* ---- renaming an object (ASSIGN_STR obN.name) ---- */

/* The name a script gives its object goes in a text of the object's own, freed with it; the
 * prototype and its other copies keep their name. */
TEST_F(ObjectTriggerTest, ARenamedObjectHasATextOfItsOwn)
{
    install_script(ON_EXAMINE_OBJECT,
        { { SCRIPT_ASSIGN_STR, "a glowing stone", { SCRIPT_PARAM_OB1_NAME } } });
    obj_data* stone = make_scripted(STONE_VNUM);
    obj_to_char(stone, &m_player);

    run_command(do_examine, "stone");

    EXPECT_STREQ("a glowing stone", stone->short_description);
    EXPECT_EQ(stone->shared_text().use_count(), 1) << "no other object shows the new name";
    EXPECT_STREQ("a grey stone", prototype_for(STONE_VNUM).short_description);
}

/* A script changes only the object's short description: it keeps the rest of the text it showed. */
TEST_F(ObjectTriggerTest, ARenamedObjectKeepsTheRestOfItsText)
{
    install_script(ON_EXAMINE_OBJECT,
        { { SCRIPT_ASSIGN_STR, "a glowing stone", { SCRIPT_PARAM_OB1_NAME } } });
    object_text::fields text = object_text_fields_of(prototype_for(STONE_VNUM));
    text.description = "A grey stone lies here.";
    text.action_description = "It hums.";
    text.extra_descriptions.push_back({ "veins", "Veins of quartz." });
    prototype_for(STONE_VNUM).set_text(create_object_text(std::move(text)));
    obj_data* stone = make_scripted(STONE_VNUM);
    obj_to_char(stone, &m_player);

    run_command(do_examine, "stone");

    EXPECT_STREQ("a glowing stone", stone->short_description);
    EXPECT_STREQ("stone grey", stone->name);
    EXPECT_STREQ("A grey stone lies here.", stone->description);
    EXPECT_STREQ("It hums.", stone->action_description);
    ASSERT_NE(nullptr, stone->ex_description);
    EXPECT_STREQ("veins", stone->ex_description->keyword);
    EXPECT_STREQ("Veins of quartz.", stone->ex_description->description);
}

/* An object a script found and renamed through ob2 or ob3 gets a text of its own, as its own object
 * does: the other copies, its prototype and the script's own object keep theirs. */
TEST_F(ObjectTriggerTest, AnObjectFoundByAScriptAndRenamedHasATextOfItsOwn)
{
    const std::pair<int, int> variables[]
        = { { SCRIPT_PARAM_OB2, SCRIPT_PARAM_OB2_NAME }, { SCRIPT_PARAM_OB3, SCRIPT_PARAM_OB3_NAME } };
    for (const std::pair<int, int>& variable : variables) {
        SCOPED_TRACE(variable.first);
        install_script(ON_EXAMINE_OBJECT,
            { { SCRIPT_ASSIGN_INV, nullptr,
                  { BREAD_VNUM, variable.first, SCRIPT_PARAM_CH1, SCRIPT_PARAM_INT1 } },
                { SCRIPT_ASSIGN_STR, "a stale loaf", { variable.second } } });
        obj_data* other_loaf = make_object(BREAD_VNUM, 0);
        obj_to_char(other_loaf, &m_player);
        obj_data* renamed = make_object(BREAD_VNUM, 0); /* first in the pack, so the script finds it */
        obj_to_char(renamed, &m_player);
        obj_data* stone = make_scripted(STONE_VNUM);
        obj_to_char(stone, &m_player);

        run_command(do_examine, "stone");

        EXPECT_STREQ("a stale loaf", renamed->short_description);
        ASSERT_NE(nullptr, renamed->shared_text());
        EXPECT_STREQ("a stale loaf", renamed->shared_text()->short_description());
        EXPECT_EQ(renamed->shared_text().use_count(), 1);
        EXPECT_STREQ("a loaf of bread", other_loaf->short_description);
        EXPECT_STREQ("a loaf of bread", prototype_for(BREAD_VNUM).short_description);
        EXPECT_STREQ("a grey stone", stone->short_description);

        extract_obj(stone);
        extract_obj(renamed);
        extract_obj(other_loaf);
    }
}

/* Renamed again, the object lets go of the name the script gave it first and keeps the new one. */
TEST_F(ObjectTriggerTest, AnObjectRenamedTwiceKeepsTheLatestName)
{
    install_script(ON_EXAMINE_OBJECT,
        { { SCRIPT_ASSIGN_STR, "a dull stone", { SCRIPT_PARAM_OB1_NAME } },
            { SCRIPT_ASSIGN_STR, "a glowing stone", { SCRIPT_PARAM_OB1_NAME } } });
    obj_data* stone = make_scripted(STONE_VNUM);
    obj_to_char(stone, &m_player);

    run_command(do_examine, "stone");

    EXPECT_STREQ("a glowing stone", stone->short_description);
    EXPECT_EQ(stone->shared_text().use_count(), 1) << "no other object shows the new name";
    EXPECT_STREQ("a grey stone", prototype_for(STONE_VNUM).short_description);
}
