/* Mob trigger scripts that remove a character from the game (EXTRACT_CHAR) while the command or
 * fight that set them off still uses it, and the variables a running script keeps.
 *
 * A removed mob is freed at once. Whatever set its trigger off stops touching it: a room's other
 * mobs still hear, see or are entered on; a give or wear of everything stops; a death or a blow
 * that lost its victim or attacker is over. A script that keeps everyone changes nothing. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../comm.h"
#include "../spells.h"

ACMD(do_say);
ACMD(do_give);
ACMD(do_wear);
ACMD(do_cast);
void one_mobile_activity(char_data* mob);
void die(char_data* dead_man, char_data* killer, int attack_type);
void raw_kill(char_data* dead_man, const char_data* killer, int attack_type);

extern struct index_data* mob_index;
extern struct char_data* mob_proto;
extern int top_of_mobt;

using namespace scripted_room;

namespace {

/* What the test scripts send ch2 just before they remove their mob. */
constexpr const char* VANISH_MESSAGE = "The mob vanishes.";
/* What a damage test deals. */
constexpr int BLOW_DAMAGE = 30;

class CharacterTriggerTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        /* Made at boot; a blow and a death ask it. Making it again keeps the one there is, and
         * blows and deaths of mobs alone record nothing in it. */
        game_rules::big_brother::create(weather_info, &world);
    }

    /* Makes script SCRIPT_VNUM one block that tells ch2 the mob vanishes and then removes the
     * mob (ch1) from the game, ending without RETURN_FALSE. */
    void install_self_removing_mob_script(int trigger_type)
    {
        install_script(trigger_type,
            { { SCRIPT_SEND_TO_CHAR, VANISH_MESSAGE, { SCRIPT_PARAM_CH2 } },
                { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    }

    /* Makes script SCRIPT_VNUM one block that only tells ch2 it ran. */
    void install_harmless_mob_script(int trigger_type)
    {
        install_script(trigger_type, { { SCRIPT_SEND_TO_CHAR, SCRIPT_RAN_MESSAGE, { SCRIPT_PARAM_CH2 } } });
    }

    /* Hands this object to the player. */
    obj_data* give_player(obj_data* object)
    {
        obj_to_char(object, &m_player);
        return object;
    }
};

} // namespace

/* ---- hearing (ON_HEAR_SAY) ---- */

TEST_F(CharacterTriggerTest, EveryMobThatRemovesItselfOnHearingHearsTheSpeaker)
{
    install_self_removing_mob_script(ON_HEAR_SAY);
    make_mob("first mob", SCRIPT_VNUM);
    make_mob("second mob", SCRIPT_VNUM);
    make_mob("third mob", SCRIPT_VNUM);

    run_command(do_say, "hello");

    EXPECT_EQ(3, count_in_output(VANISH_MESSAGE));
    EXPECT_EQ(nullptr, character_list) << "every mob removed itself";
    EXPECT_EQ(&m_player, world[0].people);
    EXPECT_EQ(nullptr, m_player.next_in_room);
}

TEST_F(CharacterTriggerTest, EveryMobThatKeepsItselfHearsTheSpeakerOnce)
{
    install_harmless_mob_script(ON_HEAR_SAY);
    make_mob("first mob", SCRIPT_VNUM);
    make_mob("second mob", SCRIPT_VNUM);

    run_command(do_say, "hello");

    EXPECT_EQ(2, count_in_output(SCRIPT_RAN_MESSAGE));
}

/* A listener's script removes the mob that spoke: the listeners after it still hear the words,
 * from nobody. */
TEST_F(CharacterTriggerTest, LaterListenersStillHearASpeakerAListenerRemoved)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_HEAR_SAY, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_HEAR_SAY,
                { { SCRIPT_SEND_TO_CHAR, "You are heard.", { SCRIPT_PARAM_CH2 } },
                    { SCRIPT_SEND_TO_ROOM, "The witness nods.", { SCRIPT_PARAM_CH1_ROOM } } } } } } });
    char_data* speaker = make_mob("speaker", 0);
    make_mob("remover", SCRIPT_VNUM);
    make_mob("witness", OTHER_SCRIPT_VNUM);

    run_command_as(speaker, do_say, "hello");

    EXPECT_FALSE(is_in_game(speaker));
    EXPECT_TRUE(output_has("The witness nods."));
}

/* What script THIRD_SCRIPT_VNUM of removes_the_next_mob_scripts tells the room. */
constexpr const char* THIRD_MOB_MESSAGE = "The third mob looks up.";

/* Script SCRIPT_VNUM says a word on <trigger_type>, script OTHER_SCRIPT_VNUM removes its own mob on
 * hearing that word, and script THIRD_SCRIPT_VNUM tells the room THIRD_MOB_MESSAGE on
 * <trigger_type>. */
static std::vector<script_spec> removes_the_next_mob_scripts(int trigger_type)
{
    return { { SCRIPT_VNUM, { { trigger_type, { { SCRIPT_DO_SAY, "vanish", { SCRIPT_PARAM_CH1 } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_HEAR_SAY,
                { { SCRIPT_IF_STR_CONTAINS, "VANISH", { SCRIPT_PARAM_STR1 } },
                    { SCRIPT_BEGIN, nullptr, {} },
                    { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } },
                    { SCRIPT_END, nullptr, {} } } } } },
        { THIRD_SCRIPT_VNUM,
            { { trigger_type, { { SCRIPT_SEND_TO_ROOM, THIRD_MOB_MESSAGE, { SCRIPT_PARAM_CH1_ROOM } } } } } } };
}

/* The first mob's answer makes the second, next in line, remove itself: the third hears the
 * answer, and then the player. */
TEST_F(CharacterTriggerTest, ALaterMobStillHearsWhenAnEarlierOnesScriptRemovesTheOneBetween)
{
    install_scripts(removes_the_next_mob_scripts(ON_HEAR_SAY));
    make_mob("first", SCRIPT_VNUM);
    char_data* second = make_mob("second", OTHER_SCRIPT_VNUM);
    make_mob("third", THIRD_SCRIPT_VNUM);

    run_command(do_say, "hello");

    EXPECT_FALSE(is_in_game(second));
    EXPECT_EQ(2, count_in_output(THIRD_MOB_MESSAGE));
}

/* ---- the say and the yell blocks of a hearing script ---- */

TEST_F(CharacterTriggerTest, AMobThatRemovesItselfOnHearingRunsNoYellBlock)
{
    install_scripts({ { SCRIPT_VNUM,
        { { ON_HEAR_SAY, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } } },
            { ON_HEAR_YELL, { { SCRIPT_SEND_TO_CHAR, "The yell block ran.", { SCRIPT_PARAM_CH2 } } } } } } });
    make_mob("mob", SCRIPT_VNUM);

    run_command(do_say, "hello");

    EXPECT_FALSE(output_has("The yell block ran."));
}

/* Hearing a say runs a mob's yell block after its say block. */
TEST_F(CharacterTriggerTest, HearingASayRunsTheYellBlockToo)
{
    install_scripts({ { SCRIPT_VNUM,
        { { ON_HEAR_SAY, { { SCRIPT_SEND_TO_CHAR, "The say block ran.", { SCRIPT_PARAM_CH2 } } } },
            { ON_HEAR_YELL, { { SCRIPT_SEND_TO_CHAR, "The yell block ran.", { SCRIPT_PARAM_CH2 } } } } } } });
    make_mob("mob", SCRIPT_VNUM);

    run_command(do_say, "hello");

    EXPECT_TRUE(output_has("The say block ran."));
    EXPECT_TRUE(output_has("The yell block ran."));
}

/* The say block removes the one who spoke: the yell block after it is told nobody spoke, so it
 * has no speaker's room to tell. */
TEST_F(CharacterTriggerTest, AYellBlockIsToldNobodySpokeOnceTheSayBlockRemovesTheSpeaker)
{
    install_scripts({ { SCRIPT_VNUM,
        { { ON_HEAR_SAY, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } } },
            { ON_HEAR_YELL,
                { { SCRIPT_SEND_TO_ROOM, "The speaker is answered.", { SCRIPT_PARAM_CH2_ROOM } },
                    { SCRIPT_SEND_TO_ROOM, "The yell block ran.", { SCRIPT_PARAM_CH1_ROOM } } } } } } });
    char_data* speaker = make_mob("speaker", 0);
    make_mob("listener", SCRIPT_VNUM);

    run_command_as(speaker, do_say, "hello");

    EXPECT_FALSE(is_in_game(speaker));
    EXPECT_TRUE(output_has("The yell block ran."));
    EXPECT_FALSE(output_has("The speaker is answered."));
}

/* ---- giving (ON_RECEIVE) ---- */

/* How many stones the player gives in the give-all tests. */
constexpr int STONES_TO_GIVE = 3;

TEST_F(CharacterTriggerTest, GivingAllToAMobThatRemovesItselfStopsAfterTheFirstItem)
{
    install_self_removing_mob_script(ON_RECEIVE);
    char_data* keeper = make_mob("keeper", SCRIPT_VNUM);
    for (int stone = 0; stone < STONES_TO_GIVE; ++stone) {
        give_player(make_object(STONE_VNUM, 0));
    }

    run_command(do_give, "all keeper");

    EXPECT_EQ(1, count_in_output(VANISH_MESSAGE));
    EXPECT_FALSE(is_in_game(keeper));
    EXPECT_EQ(STONES_TO_GIVE - 1, count_contents(m_player.carrying))
        << "the rest stay with the player";
    EXPECT_EQ(1, count_contents(world[0].contents)) << "the keeper dropped what it was given";
}

TEST_F(CharacterTriggerTest, GivingAllToAMobThatKeepsItselfGivesEverything)
{
    install_harmless_mob_script(ON_RECEIVE);
    char_data* keeper = make_mob("keeper", SCRIPT_VNUM);
    for (int stone = 0; stone < STONES_TO_GIVE; ++stone) {
        give_player(make_object(STONE_VNUM, 0));
    }

    run_command(do_give, "all keeper");

    EXPECT_EQ(STONES_TO_GIVE, count_in_output(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(STONES_TO_GIVE, count_contents(keeper->carrying));
    EXPECT_EQ(nullptr, m_player.carrying);
}

/* The keeper's script takes the bread the player was going to give next and destroys it. */
TEST_F(CharacterTriggerTest, GivingAllStopsWhenTheReceiversScriptExtractsTheNextItem)
{
    install_script(ON_RECEIVE,
        { { SCRIPT_ASSIGN_INV, nullptr,
              { BREAD_VNUM, SCRIPT_PARAM_OB2, SCRIPT_PARAM_CH2, SCRIPT_PARAM_INT1 } },
            { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB2 } } });
    char_data* keeper = make_mob("keeper", SCRIPT_VNUM);
    obj_data* bread = give_player(make_object(BREAD_VNUM, 0));
    obj_data* stone = give_player(make_object(STONE_VNUM, 0)); /* given first */

    run_command(do_give, "all keeper");

    EXPECT_EQ(stone, keeper->carrying);
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_FALSE(is_in_object_list(bread));
}

TEST_F(CharacterTriggerTest, GivingAllOfAKindStopsWhenTheReceiversScriptExtractsTheNextOne)
{
    install_script(ON_RECEIVE,
        { { SCRIPT_ASSIGN_INV, nullptr,
              { STONE_VNUM, SCRIPT_PARAM_OB2, SCRIPT_PARAM_CH2, SCRIPT_PARAM_INT1 } },
            { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB2 } } });
    char_data* keeper = make_mob("keeper", SCRIPT_VNUM);
    give_player(make_object(STONE_VNUM, 0));
    give_player(make_object(STONE_VNUM, 0));

    run_command(do_give, "all.stone keeper");

    EXPECT_EQ(1, count_contents(keeper->carrying));
    EXPECT_EQ(nullptr, m_player.carrying);
}

/* The keeper's script makes the player drop the bread it was going to give next: the giving stops,
 * and the bread stays on the floor. */
TEST_F(CharacterTriggerTest, GivingAllStopsWhenTheReceiversScriptDropsTheNextItem)
{
    install_script(ON_RECEIVE,
        { { SCRIPT_ASSIGN_INV, nullptr,
              { BREAD_VNUM, SCRIPT_PARAM_OB2, SCRIPT_PARAM_CH2, SCRIPT_PARAM_INT1 } },
            { SCRIPT_DO_DROP, nullptr, { SCRIPT_PARAM_CH2, SCRIPT_PARAM_OB2 } } });
    char_data* keeper = make_mob("keeper", SCRIPT_VNUM);
    obj_data* bread = give_player(make_object(BREAD_VNUM, 0));
    obj_data* stone = give_player(make_object(STONE_VNUM, 0)); /* given first */

    run_command(do_give, "all keeper");

    EXPECT_EQ(stone, keeper->carrying);
    EXPECT_EQ(nullptr, stone->next_content);
    EXPECT_EQ(bread, world[0].contents);
    EXPECT_EQ(nullptr, m_player.carrying);
}

/* The keeper's script removes the giver: the giving stops, and the giver's other stone falls to
 * the floor with it. */
TEST_F(CharacterTriggerTest, GivingAllOfAKindStopsWhenTheReceiversScriptRemovesTheGiver)
{
    install_script(ON_RECEIVE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    char_data* giver = make_mob("giver", 0);
    char_data* keeper = make_mob("keeper", SCRIPT_VNUM);
    obj_to_char(make_object(STONE_VNUM, 0), giver);
    obj_to_char(make_object(STONE_VNUM, 0), giver);

    run_command_as(giver, do_give, "all.stone keeper");

    EXPECT_FALSE(is_in_game(giver));
    EXPECT_EQ(1, count_contents(keeper->carrying));
    EXPECT_EQ(1, count_contents(world[0].contents));
}

/* The same for giving all of a kind: the script drops the second stone. */
TEST_F(CharacterTriggerTest, GivingAllOfAKindStopsWhenTheReceiversScriptDropsTheNextOne)
{
    install_script(ON_RECEIVE,
        { { SCRIPT_ASSIGN_INV, nullptr,
              { STONE_VNUM, SCRIPT_PARAM_OB2, SCRIPT_PARAM_CH2, SCRIPT_PARAM_INT1 } },
            { SCRIPT_DO_DROP, nullptr, { SCRIPT_PARAM_CH2, SCRIPT_PARAM_OB2 } } });
    char_data* keeper = make_mob("keeper", SCRIPT_VNUM);
    give_player(make_object(STONE_VNUM, 0));
    give_player(make_object(STONE_VNUM, 0));

    run_command(do_give, "all.stone keeper");

    EXPECT_EQ(1, count_contents(keeper->carrying));
    EXPECT_EQ(1, count_contents(world[0].contents));
    EXPECT_EQ(nullptr, m_player.carrying);
}

/* The keeper's script has the player hand over the bread itself: the bread is the keeper's, and
 * the giving does not take it from the keeper to give it again. */
TEST_F(CharacterTriggerTest, GivingAllStopsWhenTheReceiversScriptTakesTheNextItem)
{
    install_script(ON_RECEIVE,
        { { SCRIPT_ASSIGN_INV, nullptr,
              { BREAD_VNUM, SCRIPT_PARAM_OB2, SCRIPT_PARAM_CH2, SCRIPT_PARAM_INT1 } },
            { SCRIPT_DO_GIVE, nullptr, { SCRIPT_PARAM_CH2, SCRIPT_PARAM_CH1, SCRIPT_PARAM_OB2 } } });
    char_data* keeper = make_mob("keeper", SCRIPT_VNUM);
    give_player(make_object(BREAD_VNUM, 0));
    give_player(make_object(STONE_VNUM, 0)); /* given first */

    run_command(do_give, "all keeper");

    EXPECT_EQ(2, count_contents(keeper->carrying));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(2, count_in_output("You give")) << "the stone by the command, the bread by the script";
}

/* ---- wearing (ON_WEAR) ---- */

/* The ring's script destroys the stone that wear all would look at next. */
TEST_F(CharacterTriggerTest, WearingAllStopsWhenARingsScriptExtractsTheNextItem)
{
    install_script(ON_WEAR,
        { { SCRIPT_ASSIGN_INV, nullptr,
              { STONE_VNUM, SCRIPT_PARAM_OB2, SCRIPT_PARAM_CH1, SCRIPT_PARAM_INT1 } },
            { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB2 } } });
    obj_data* stone = give_player(make_object(STONE_VNUM, 0));
    obj_data* ring = give_player(make_scripted(RING_VNUM)); /* looked at first */

    run_command(do_wear, "all");

    EXPECT_EQ(ring, m_player.equipment[WEAR_FINGER_R]);
    EXPECT_FALSE(is_in_object_list(stone));
}

TEST_F(CharacterTriggerTest, AMobWearingAllStopsWhenARingsScriptRemovesIt)
{
    install_script(ON_WEAR, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* wearer = make_mob("wearer", 0);
    obj_to_char(make_object(RING_VNUM, 0), wearer);
    obj_to_char(make_scripted(RING_VNUM), wearer); /* looked at first */

    run_command_as(wearer, do_wear, "all");

    EXPECT_FALSE(is_in_game(wearer));
    EXPECT_EQ(2, count_contents(world[0].contents)) << "the wearer dropped both rings";
}

TEST_F(CharacterTriggerTest, AMobWearingAllOfAKindStopsWhenARingsScriptRemovesIt)
{
    install_script(ON_WEAR, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* wearer = make_mob("wearer", 0);
    obj_to_char(make_object(RING_VNUM, 0), wearer);
    obj_to_char(make_scripted(RING_VNUM), wearer); /* looked at first */

    run_command_as(wearer, do_wear, "all.ring");

    EXPECT_FALSE(is_in_game(wearer));
    EXPECT_EQ(2, count_contents(world[0].contents)) << "the wearer dropped both rings";
}

/* The stone answers to "ring" too, so wear all.ring looks at it next; the ring's script destroys
 * it. */
TEST_F(CharacterTriggerTest, WearingAllOfAKindStopsWhenARingsScriptExtractsTheNextOne)
{
    install_script(ON_WEAR,
        { { SCRIPT_ASSIGN_INV, nullptr,
              { STONE_VNUM, SCRIPT_PARAM_OB2, SCRIPT_PARAM_CH1, SCRIPT_PARAM_INT1 } },
            { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB2 } } });
    prototype_for(STONE_VNUM).name = prototype_for(RING_VNUM).name;
    obj_data* stone = give_player(make_object(STONE_VNUM, 0));
    obj_data* ring = give_player(make_scripted(RING_VNUM)); /* looked at first */

    run_command(do_wear, "all.ring");

    EXPECT_EQ(ring, m_player.equipment[WEAR_FINGER_R]);
    EXPECT_FALSE(is_in_object_list(stone));
}

TEST_F(CharacterTriggerTest, WearingAllOfAKindWearsEveryRingWhoseScriptKeepsIt)
{
    install_harmless_script(ON_WEAR);
    obj_data* first = give_player(make_scripted(RING_VNUM));
    obj_data* second = give_player(make_scripted(RING_VNUM));

    run_command(do_wear, "all.ring");

    EXPECT_EQ(2, count_in_output(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(second, m_player.equipment[WEAR_FINGER_R]);
    EXPECT_EQ(first, m_player.equipment[WEAR_FINGER_L]);
}

TEST_F(CharacterTriggerTest, WearingAllWearsEveryRingWhoseScriptKeepsIt)
{
    install_harmless_script(ON_WEAR);
    obj_data* first = give_player(make_scripted(RING_VNUM));
    obj_data* second = give_player(make_scripted(RING_VNUM));

    run_command(do_wear, "all");

    EXPECT_EQ(2, count_in_output(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(second, m_player.equipment[WEAR_FINGER_R]);
    EXPECT_EQ(first, m_player.equipment[WEAR_FINGER_L]);
}

/* ---- entering a room (ON_ENTER and ON_BEFORE_ENTER on the room's mobs) ---- */

TEST_F(CharacterTriggerTest, EveryMobThatRemovesItselfOnSomeoneEnteringReacts)
{
    install_self_removing_mob_script(ON_ENTER);
    make_mob("first mob", SCRIPT_VNUM);
    make_mob("second mob", SCRIPT_VNUM);
    make_mob("third mob", SCRIPT_VNUM);

    call_trigger(ON_ENTER, &world[0], &m_player, 0);

    EXPECT_EQ(3, count_in_output(VANISH_MESSAGE));
    EXPECT_EQ(nullptr, character_list);
}

TEST_F(CharacterTriggerTest, EveryMobThatRemovesItselfBeforeSomeoneEntersReactsAndLetsThemIn)
{
    install_self_removing_mob_script(ON_BEFORE_ENTER);
    make_mob("first mob", SCRIPT_VNUM);
    make_mob("second mob", SCRIPT_VNUM);
    make_mob("third mob", SCRIPT_VNUM);

    EXPECT_EQ(1, call_trigger(ON_BEFORE_ENTER, &world[0], &m_player, 0));

    EXPECT_EQ(3, count_in_output(VANISH_MESSAGE));
}

TEST_F(CharacterTriggerTest, EveryMobThatKeepsItselfReactsToSomeoneEntering)
{
    install_harmless_mob_script(ON_ENTER);
    make_mob("first mob", SCRIPT_VNUM);
    make_mob("second mob", SCRIPT_VNUM);

    call_trigger(ON_ENTER, &world[0], &m_player, 0);

    EXPECT_EQ(2, count_in_output(SCRIPT_RAN_MESSAGE));
}

/* A guard removes the mob that walked in: the watcher and the stone after it still react, told
 * nobody is entering. The stone's script removes the stone, and has nobody's room to tell. */
TEST_F(CharacterTriggerTest, LaterMobsStillReactWhenAMobEnteringIsRemoved)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_ENTER, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_ENTER,
                { { SCRIPT_SEND_TO_CHAR, "You are watched.", { SCRIPT_PARAM_CH2 } },
                    { SCRIPT_SEND_TO_ROOM, "The watcher looks up.", { SCRIPT_PARAM_CH1_ROOM } } } } } },
        { THIRD_SCRIPT_VNUM,
            { { ON_ENTER,
                { { SCRIPT_SEND_TO_ROOM, "Someone is there.", { SCRIPT_PARAM_CH1_ROOM } },
                    { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB1 } } } } } } });
    make_mob("guard", SCRIPT_VNUM);
    make_mob("watcher", OTHER_SCRIPT_VNUM);
    obj_data* stone = make_object(STONE_VNUM, THIRD_SCRIPT_VNUM);
    obj_to_room(stone, 0);
    char_data* wanderer = make_mob("wanderer", 0);

    EXPECT_EQ(1, call_trigger(ON_ENTER, &world[0], wanderer, 0));

    EXPECT_FALSE(is_in_game(wanderer));
    EXPECT_TRUE(output_has("The watcher looks up."));
    EXPECT_FALSE(is_in_object_list(stone)) << "its script ran";
    EXPECT_FALSE(output_has("Someone is there."));
}

/* The first mob's greeting makes the second, next in line, remove itself: the third still
 * reacts. */
TEST_F(CharacterTriggerTest, ALaterMobStillReactsWhenAnEarlierOnesScriptRemovesTheOneBetween)
{
    install_scripts(removes_the_next_mob_scripts(ON_ENTER));
    make_mob("first", SCRIPT_VNUM);
    char_data* second = make_mob("second", OTHER_SCRIPT_VNUM);
    make_mob("third", THIRD_SCRIPT_VNUM);

    EXPECT_EQ(1, call_trigger(ON_ENTER, &world[0], &m_player, 0));

    EXPECT_FALSE(is_in_game(second));
    EXPECT_TRUE(output_has(THIRD_MOB_MESSAGE));
}

/* The first mob's script returns false: the mobs after it do not react, and whoever was entering
 * is kept out. */
TEST_F(CharacterTriggerTest, AMobThatRefusesKeepsTheMobsAfterItFromReacting)
{
    install_scripts({ { SCRIPT_VNUM, { { ON_BEFORE_ENTER, { { SCRIPT_RETURN_FALSE, nullptr, {} } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_BEFORE_ENTER,
                { { SCRIPT_SEND_TO_ROOM, "The watcher looks up.", { SCRIPT_PARAM_CH1_ROOM } } } } } } });
    make_mob("guard", SCRIPT_VNUM);
    make_mob("watcher", OTHER_SCRIPT_VNUM);

    EXPECT_EQ(0, call_trigger(ON_BEFORE_ENTER, &world[0], &m_player, 0));

    EXPECT_FALSE(output_has("The watcher looks up."));
}

TEST_F(CharacterTriggerTest, LaterMobsStillReactBeforeAMobEnteringIsRemoved)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_BEFORE_ENTER, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_BEFORE_ENTER,
                { { SCRIPT_SEND_TO_CHAR, "You are watched.", { SCRIPT_PARAM_CH2 } },
                    { SCRIPT_SEND_TO_ROOM, "The watcher looks up.", { SCRIPT_PARAM_CH1_ROOM } } } } } } });
    make_mob("guard", SCRIPT_VNUM);
    make_mob("watcher", OTHER_SCRIPT_VNUM);
    char_data* wanderer = make_mob("wanderer", 0);

    EXPECT_EQ(1, call_trigger(ON_BEFORE_ENTER, &world[0], wanderer, 0));

    EXPECT_FALSE(is_in_game(wanderer));
    EXPECT_TRUE(output_has("The watcher looks up."));
}

/* ---- dying (ON_DIE) ---- */

/* Experience a victim is worth gaining from. */
constexpr int EXPERIENCE_WORTH_GAINING = 5000;

/* A mob that its own death script removes has had its end there: its killer gains nothing and no
 * corpse is made. The mob's things are left in the room, as EXTRACT_CHAR leaves them, and the rest
 * of the script finds ch1 empty. */
TEST_F(CharacterTriggerTest, AMobThatRemovesItselfAsItDiesGivesNothingAndLeavesNoCorpse)
{
    install_script(ON_DIE,
        { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } },
            { SCRIPT_SEND_TO_ROOM, "The dying one lingers.", { SCRIPT_PARAM_CH1_ROOM } } });
    char_data* victim = make_mob("victim", SCRIPT_VNUM);
    victim->points.exp = EXPERIENCE_WORTH_GAINING;
    obj_data* stone = make_object(STONE_VNUM, 0);
    obj_to_char(stone, victim);
    const int experience_before = GET_EXP(&m_player);

    die(victim, &m_player, 0);

    EXPECT_FALSE(is_in_game(victim));
    EXPECT_EQ(experience_before, GET_EXP(&m_player));
    EXPECT_EQ(stone, world[0].contents) << "its things are left in the room, and no corpse";
    EXPECT_EQ(nullptr, stone->next_content);
    EXPECT_FALSE(output_has("The dying one lingers.")) << "ch1 is empty: nobody's room to tell";
}

/* The same for a death script that kills its own mob outright (RAW_KILL ch1): that kill makes the
 * mob's corpse, and the death makes no second one. */
TEST_F(CharacterTriggerTest, AMobItsDeathScriptKillsOutrightGivesNothingAndHasOneCorpse)
{
    install_script(ON_DIE, { { SCRIPT_RAW_KILL, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* victim = make_mob("victim", SCRIPT_VNUM);
    victim->points.exp = EXPERIENCE_WORTH_GAINING;
    /* Its corpse is marked with its prototype's number. */
    index_data prototype_index[1] {};
    index_data* saved_mob_index = mob_index;
    const int saved_top_of_mobt = top_of_mobt;
    mob_index = prototype_index;
    top_of_mobt = 0;
    /* With a prototype number free_char leaves the mob's strings and profession block to the
     * prototype, so the test frees them. */
    char* victim_name = victim->player.name;
    char* victim_short_description = victim->player.short_descr;
    char_prof_data* victim_professions = victim->profs;
    victim->nr = 0;
    const int experience_before = GET_EXP(&m_player);

    die(victim, &m_player, 0);

    mob_index = saved_mob_index;
    top_of_mobt = saved_top_of_mobt;
    EXPECT_FALSE(is_in_game(victim));
    EXPECT_EQ(experience_before, GET_EXP(&m_player));
    RELEASE(victim_name);
    RELEASE(victim_short_description);
    RELEASE(victim_professions);
    ASSERT_NE(nullptr, world[0].contents);
    EXPECT_EQ(nullptr, world[0].contents->next_content) << "one corpse";
}

/* The same when the death script also refuses the death: the mob is gone, and nothing more. */
TEST_F(CharacterTriggerTest, AMobThatRemovesItselfAndRefusesToDieGivesNothing)
{
    install_script(ON_DIE,
        { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } }, { SCRIPT_RETURN_FALSE, nullptr, {} } });
    char_data* victim = make_mob("victim", SCRIPT_VNUM);
    victim->points.exp = EXPERIENCE_WORTH_GAINING;
    const int experience_before = GET_EXP(&m_player);

    die(victim, &m_player, 0);

    EXPECT_FALSE(is_in_game(victim));
    EXPECT_EQ(experience_before, GET_EXP(&m_player));
    EXPECT_EQ(nullptr, world[0].contents);
}

TEST_F(CharacterTriggerTest, AMobWhoseDieScriptRefusesStaysInTheRoom)
{
    install_script(ON_DIE, { { SCRIPT_RETURN_FALSE, nullptr, {} } });
    char_data* victim = make_mob("victim", SCRIPT_VNUM);

    die(victim, nullptr, 0);

    EXPECT_TRUE(is_in_game(victim));
    EXPECT_EQ(0, victim->in_room);
}

/* ---- damage (ON_DAMAGE on the victim) ---- */

/* damage() reports a victim its own script removed the way it reports a death. */
TEST_F(CharacterTriggerTest, ABlowEndsWhenTheVictimsScriptRemovesIt)
{
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* attacker = make_mob("attacker", 0);
    char_data* victim = make_mob("victim", SCRIPT_VNUM);

    EXPECT_EQ(1, damage(attacker, victim, BLOW_DAMAGE, TYPE_HIT, 0));

    EXPECT_FALSE(is_in_game(victim));
    EXPECT_TRUE(is_in_game(attacker));
}

TEST_F(CharacterTriggerTest, ABlowEndsWhenTheVictimsScriptRemovesTheAttacker)
{
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    char_data* attacker = make_mob("attacker", 0);
    char_data* victim = make_mob("victim", SCRIPT_VNUM);

    EXPECT_EQ(0, damage(attacker, victim, BLOW_DAMAGE, TYPE_HIT, 0));

    EXPECT_FALSE(is_in_game(attacker));
    EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(victim)) << "no damage was dealt";
}

TEST_F(CharacterTriggerTest, ABlowLandsWhenTheVictimsScriptKeepsBoth)
{
    install_harmless_mob_script(ON_DAMAGE);
    char_data* attacker = make_mob("attacker", 0);
    char_data* victim = make_mob("victim", SCRIPT_VNUM);

    EXPECT_EQ(0, damage(attacker, victim, BLOW_DAMAGE, TYPE_HIT, 0));

    EXPECT_EQ(MOB_HIT_POINTS - BLOW_DAMAGE, GET_HIT(victim));
}

/* The victim's script removes the victim: the attacker's weapon is not looked at. */
TEST_F(CharacterTriggerTest, AWeaponsDamageScriptDoesNotRunForAVictimItsOwnScriptRemoved)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_DAMAGE, { { SCRIPT_SEND_TO_ROOM, "The sword hums.", { SCRIPT_PARAM_CH2_ROOM } } } } } } });
    char_data* attacker = make_mob("attacker", 0);
    equip_char(attacker, make_object(SWORD_VNUM, OTHER_SCRIPT_VNUM), WIELD);
    char_data* victim = make_mob("victim", SCRIPT_VNUM);

    EXPECT_EQ(0, call_trigger(ON_DAMAGE, victim, attacker, 0));

    EXPECT_FALSE(is_in_game(victim));
    EXPECT_FALSE(output_has("The sword hums."));
}

TEST_F(CharacterTriggerTest, AWeaponsDamageScriptRunsWhenTheVictimsScriptKeepsBoth)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_DAMAGE, { { SCRIPT_SEND_TO_ROOM, "The victim winces.", { SCRIPT_PARAM_CH1_ROOM } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_DAMAGE, { { SCRIPT_SEND_TO_ROOM, "The sword hums.", { SCRIPT_PARAM_CH2_ROOM } } } } } } });
    char_data* attacker = make_mob("attacker", 0);
    equip_char(attacker, make_object(SWORD_VNUM, OTHER_SCRIPT_VNUM), WIELD);
    char_data* victim = make_mob("victim", SCRIPT_VNUM);

    EXPECT_EQ(1, call_trigger(ON_DAMAGE, victim, attacker, 0));

    EXPECT_TRUE(output_has("The victim winces."));
    EXPECT_TRUE(output_has("The sword hums."));
}

/* The weapon's script removes the victim: no damage is to be dealt. */
TEST_F(CharacterTriggerTest, AWeaponsDamageScriptThatRemovesTheVictimEndsTheBlow)
{
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* attacker = make_mob("attacker", 0);
    equip_char(attacker, make_scripted(SWORD_VNUM), WIELD);
    char_data* victim = make_mob("victim", 0);

    EXPECT_EQ(0, call_trigger(ON_DAMAGE, victim, attacker, 0));

    EXPECT_FALSE(is_in_game(victim));
}

/* ---- the variables of a running script ---- */

/* The mob's script runs on after it removed its own mob, with its variables intact. */
TEST_F(CharacterTriggerTest, AMobsScriptRunsOnAfterItRemovesTheMob)
{
    install_script(ON_HEAR_SAY,
        { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } },
            { SCRIPT_SEND_TO_CHAR, "Still running.", { SCRIPT_PARAM_CH2 } } });
    make_mob("speaker", SCRIPT_VNUM);

    run_command(do_say, "hello");

    EXPECT_TRUE(output_has("Still running."));
}

/* ---- scripts paused with DO_WAIT ---- */

/* How long a test script pauses (DO_WAIT): one pulse, so the next pass over the waiting list
 * resumes it. */
constexpr int PAUSE_PULSES = 1;

/* A script body that pauses for one pulse, then tells the room the mob fades and removes it. */
static std::vector<body_line> pause_then_remove_body()
{
    return { { SCRIPT_DO_WAIT, nullptr, { PAUSE_PULSES } },
        { SCRIPT_SEND_TO_ROOM, "The mob fades.", { SCRIPT_PARAM_CH1_ROOM } },
        { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } };
}

TEST_F(CharacterTriggerTest, TheWaitingListLetsGoOfMobsTheirResumedScriptsRemoved)
{
    install_script(ON_HEAR_SAY, pause_then_remove_body());
    make_mob("first mob", SCRIPT_VNUM);
    make_mob("second mob", SCRIPT_VNUM);
    run_command(do_say, "hello");
    ASSERT_NE(nullptr, waiting_list) << "both mobs paused";

    process_waiting_list();

    EXPECT_EQ(2, count_in_output("The mob fades."));
    EXPECT_EQ(nullptr, character_list);
    EXPECT_EQ(nullptr, waiting_list);
}

/* How long the second waiter waits: longer than the first one's pause, so it still waits when the
 * first resumes. */
constexpr int LONGER_WAIT_PULSES = 5;

/* The first waiter's resumed script makes the waiter after it vanish and then removes its own
 * mob: the waiting list's pass ends there, and says so in the log. */
TEST_F(CharacterTriggerTest, AWaitingListPassEndsWhenAResumedScriptRemovesTheNextWaiterToo)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_HEAR_SAY,
                              { { SCRIPT_DO_WAIT, nullptr, { PAUSE_PULSES } },
                                  { SCRIPT_DO_SAY, "vanish", { SCRIPT_PARAM_CH1 } },
                                  { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_HEAR_SAY,
                { { SCRIPT_IF_STR_CONTAINS, "VANISH", { SCRIPT_PARAM_STR1 } },
                    { SCRIPT_BEGIN, nullptr, {} },
                    { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } },
                    { SCRIPT_END, nullptr, {} } } } } } });
    char_data* first = make_mob("first", SCRIPT_VNUM);
    char_data* second = make_mob("second", OTHER_SCRIPT_VNUM);
    run_command(do_say, "hello");
    ASSERT_EQ(first, waiting_list) << "the first mob paused";
    /* A wait with no command that leaves the second mob hearing, after the first in the list. */
    ASSERT_TRUE(WAIT_STATE_BRIEF(second, LONGER_WAIT_PULSES, 0, 0, 0, 0));
    ASSERT_EQ(second, first->delay.next);

    testing::internal::CaptureStderr();
    process_waiting_list();
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(is_in_game(first));
    EXPECT_FALSE(is_in_game(second));
    EXPECT_EQ(nullptr, waiting_list);
    EXPECT_NE(std::string::npos,
        logged.find("SYSERR: process_waiting_list: the next waiter was freed; the rest wait for "
                    "the next pulse."))
        << logged;
}

/* ---- rooms lent while knocking ---- */

/* A character freed by free_char alone while lent a room ends the lend, so going out of scope
 * the lend gives no room back to freed memory, and an older lend of someone else still ends as
 * usual. */
TEST_F(CharacterTriggerTest, FreeingACharacterEndsItsRoomLend)
{
    owned_character visitor(MOB_ISNPC);
    SET_BIT(MOB_FLAGS(visitor), MOB_ISNPC);
    visitor->nr = -1; /* no prototype: free_char releases its strings */
    visitor->player.name = str_dup("visitor");
    register_npc_char(visitor);
    /* In room 0 as far as it knows, but on no list, so nothing else ends the lend. */
    visitor->in_room = 0;
    {
        room_lend player_lend(&m_player, SECOND_ROOM);
        {
            room_lend visitor_lend(visitor, SECOND_ROOM);
            /* Freed as the game frees it, while still lent the room. */
            free_char(visitor.release());
        }
        EXPECT_EQ(SECOND_ROOM, m_player.in_room) << "the player's lend outlives the visitor's";
    }
    EXPECT_EQ(0, m_player.in_room);
}

/* ---- text being edited ---- */

/* A character freed while an edit points into any of the text "string char" can edit ends the edit,
 * so no line typed later goes into the freed character. */
TEST_F(CharacterTriggerTest, FreeingACharacterEndsAnEditOfAnyOfItsText)
{
    descriptor_list = &m_descriptor;
    int fields_edited = 0;
    for (char* char_player_data::*const field :
        { &char_player_data::name, &char_player_data::title, &char_player_data::short_descr,
            &char_player_data::long_descr, &char_player_data::description }) {
        SCOPED_TRACE(fields_edited);
        char_data* mob = make_mob("statue", 0);
        m_descriptor.str = &(mob->player.*field);
        m_descriptor.str_limit = MAX_NOTE_LENGTH;

        extract_char(mob);

        EXPECT_EQ(nullptr, m_descriptor.str);
        EXPECT_EQ(0, m_descriptor.str_limit);
        ++fields_edited;
        EXPECT_EQ(fields_edited, count_in_output("What you were writing is gone.")) << output();
    }
}

/* A player freed while typing its own description ends the edit too, as its describe command leaves
 * one open. */
TEST_F(CharacterTriggerTest, FreeingAPlayerEndsAnEditOfItsOwnDescription)
{
    descriptor_list = &m_descriptor;
    {
        owned_character typist;
        typist->desc = &m_descriptor;
        m_descriptor.character = typist;
        m_descriptor.str = &typist->player.description;
    }
    m_descriptor.character = &m_player;

    EXPECT_EQ(nullptr, m_descriptor.str);
    EXPECT_TRUE(output_has("What you were writing is gone.")) << output();
}
