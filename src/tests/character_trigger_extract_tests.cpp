/* Mob trigger scripts that remove a character from the game (EXTRACT_CHAR) while the command or
 * fight that set them off still uses it, and the variables a running script keeps.
 *
 * A removed mob is freed at once. Whatever set its trigger off stops touching it: a room's other
 * mobs still hear, see or are entered on; a give or wear of everything stops; a death or a blow
 * that lost its victim or attacker is over. A script that keeps everyone changes nothing. */
#include "scripted_room_fixture.h"

#include "../comm.h"
#include "../spells.h"

void do_say(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_give(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_wear(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void die(char_data* dead_man, char_data* killer, int attack_type);
void raw_kill(char_data* ch, const char_data* killer, int attacktype);

extern struct index_data* mob_index;
extern struct char_data* mob_proto;
extern int top_of_mobt;
struct char_data* read_mobile(int nr, int type);

using namespace scripted_room;

namespace {

/* What the test scripts send ch2 just before they remove their mob. */
constexpr const char* VANISH_MESSAGE = "The mob vanishes.";
/* What a damage test deals. */
constexpr int BLOW_DAMAGE = 30;

class CharacterTriggerTest : public ScriptedRoomFixture {
protected:
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

/* A listener's script removes the mob that spoke: nobody after it hears a speaker that is gone. */
TEST_F(CharacterTriggerTest, NoOneElseHearsASpeakerThatAListenerRemoved)
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
    EXPECT_FALSE(output_has("The witness nods."));
}

/* ---- giving (ON_RECEIVE) ---- */

TEST_F(CharacterTriggerTest, GivingAllToAMobThatRemovesItselfStopsAfterTheFirstItem)
{
    install_self_removing_mob_script(ON_RECEIVE);
    char_data* keeper = make_mob("keeper", SCRIPT_VNUM);
    for (int stone = 0; stone < 3; ++stone) {
        give_player(make_object(STONE_VNUM, 0));
    }

    run_command(do_give, "all keeper");

    EXPECT_EQ(1, count_in_output(VANISH_MESSAGE));
    EXPECT_FALSE(is_in_game(keeper));
    EXPECT_EQ(2, count_contents(m_player.carrying)) << "the rest stay with the player";
    EXPECT_EQ(1, count_contents(world[0].contents)) << "the keeper dropped what it was given";
}

TEST_F(CharacterTriggerTest, GivingAllToAMobThatKeepsItselfGivesEverything)
{
    install_harmless_mob_script(ON_RECEIVE);
    char_data* keeper = make_mob("keeper", SCRIPT_VNUM);
    for (int stone = 0; stone < 3; ++stone) {
        give_player(make_object(STONE_VNUM, 0));
    }

    run_command(do_give, "all keeper");

    EXPECT_EQ(3, count_in_output(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(3, count_contents(keeper->carrying));
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

/* The ring's script drops it: there is nothing left in the player's hands to wear. */
TEST_F(CharacterTriggerTest, ARingWhoseScriptDropsItIsLeftOnTheFloor)
{
    install_script(ON_WEAR, { { SCRIPT_DO_DROP, nullptr, { SCRIPT_PARAM_CH1, SCRIPT_PARAM_OB1 } } });
    obj_data* ring = give_player(make_scripted(RING_VNUM));

    run_command(do_wear, "ring");

    EXPECT_EQ(ring, world[0].contents);
    EXPECT_EQ(nullptr, m_player.equipment[WEAR_FINGER_R]);
    EXPECT_EQ(nullptr, m_player.equipment[WEAR_FINGER_L]);
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

/* A guard removes the mob that walked in: the watcher and the stone after it never see it. */
TEST_F(CharacterTriggerTest, NoOneElseMeetsAMobRemovedAsItEnters)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_ENTER, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_ENTER,
                { { SCRIPT_SEND_TO_CHAR, "You are watched.", { SCRIPT_PARAM_CH2 } },
                    { SCRIPT_SEND_TO_ROOM, "The watcher looks up.", { SCRIPT_PARAM_CH1_ROOM } } } } } },
        { THIRD_SCRIPT_VNUM,
            { { ON_ENTER, { { SCRIPT_SEND_TO_CHAR, "The stone glints.", { SCRIPT_PARAM_CH1 } } } } } } });
    make_mob("guard", SCRIPT_VNUM);
    make_mob("watcher", OTHER_SCRIPT_VNUM);
    obj_to_room(make_object(STONE_VNUM, THIRD_SCRIPT_VNUM), 0);
    char_data* wanderer = make_mob("wanderer", 0);

    call_trigger(ON_ENTER, &world[0], wanderer, 0);

    EXPECT_FALSE(is_in_game(wanderer));
    EXPECT_FALSE(output_has("The watcher looks up."));
}

TEST_F(CharacterTriggerTest, AMobRemovedBeforeItEntersIsKeptOut)
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

    EXPECT_EQ(0, call_trigger(ON_BEFORE_ENTER, &world[0], wanderer, 0));

    EXPECT_FALSE(is_in_game(wanderer));
    EXPECT_FALSE(output_has("The watcher looks up."));
}

/* ---- dying (ON_DIE) ---- */

TEST_F(CharacterTriggerTest, AMobThatRemovesItselfAsItDiesLeavesNoCorpseAndGivesNothing)
{
    install_script(ON_DIE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* victim = make_mob("victim", SCRIPT_VNUM);
    const int experience_before = GET_EXP(&m_player);

    die(victim, &m_player, 0);

    EXPECT_FALSE(is_in_game(victim));
    EXPECT_EQ(nullptr, world[0].contents) << "no corpse";
    EXPECT_EQ(experience_before, GET_EXP(&m_player));
}

TEST_F(CharacterTriggerTest, AMobThatRemovesItselfAndRefusesToDieIsNotTouchedAgain)
{
    install_script(ON_DIE,
        { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } }, { SCRIPT_RETURN_FALSE, nullptr, {} } });
    char_data* victim = make_mob("victim", SCRIPT_VNUM);

    die(victim, nullptr, 0);

    EXPECT_FALSE(is_in_game(victim));
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

/* The ring's own wear script destroys it while the mob's script, which wore it, still holds it in
 * a second variable: that variable is emptied too. */
TEST_F(CharacterTriggerTest, AnObjectFreedByANestedScriptIsGoneFromEveryVariable)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_HEAR_SAY,
                              { { SCRIPT_ASSIGN_INV, nullptr,
                                    { RING_VNUM, SCRIPT_PARAM_OB2, SCRIPT_PARAM_CH1, SCRIPT_PARAM_INT1 } },
                                  { SCRIPT_ASSIGN_INV, nullptr,
                                      { RING_VNUM, SCRIPT_PARAM_OB3, SCRIPT_PARAM_CH1, SCRIPT_PARAM_INT1 } },
                                  { SCRIPT_DO_WEAR, nullptr, { SCRIPT_PARAM_CH1, SCRIPT_PARAM_OB2 } },
                                  { SCRIPT_SEND_TO_CHAR, "It was [%s].",
                                      { SCRIPT_PARAM_CH2, SCRIPT_PARAM_OB3_NAME } } } } } },
        { OTHER_SCRIPT_VNUM, { { ON_WEAR, { { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB1 } } } } } } });
    char_data* wearer = make_mob("wearer", SCRIPT_VNUM);
    obj_data* ring = make_object(RING_VNUM, OTHER_SCRIPT_VNUM);
    obj_to_char(ring, wearer);

    run_command(do_say, "hello");

    EXPECT_FALSE(is_in_object_list(ring));
    EXPECT_TRUE(output_has("It was []."));
}

/* The first mob's script makes it speak, the second mob answers, and the first mob hears the
 * answer: its own hearing script runs again inside the first, which still has its variables when
 * it carries on. */
TEST_F(CharacterTriggerTest, AMobsScriptKeepsItsVariablesWhenItsTriggerRunsAgainInsideIt)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_HEAR_SAY,
                              { { SCRIPT_IF_STR_CONTAINS, "HELLO", { SCRIPT_PARAM_STR1 } },
                                  { SCRIPT_BEGIN, nullptr, {} },
                                  { SCRIPT_ASSIGN_INV, nullptr,
                                      { STONE_VNUM, SCRIPT_PARAM_OB2, SCRIPT_PARAM_CH1, SCRIPT_PARAM_INT1 } },
                                  { SCRIPT_DO_SAY, "ping", { SCRIPT_PARAM_CH1 } },
                                  { SCRIPT_SEND_TO_CHAR, "Holding %s.",
                                      { SCRIPT_PARAM_CH2, SCRIPT_PARAM_OB2_NAME } },
                                  { SCRIPT_END, nullptr, {} } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_HEAR_SAY,
                { { SCRIPT_IF_STR_CONTAINS, "PING", { SCRIPT_PARAM_STR1 } },
                    { SCRIPT_BEGIN, nullptr, {} }, { SCRIPT_DO_SAY, "pong", { SCRIPT_PARAM_CH1 } },
                    { SCRIPT_END, nullptr, {} } } } } } });
    char_data* asker = make_mob("asker", SCRIPT_VNUM);
    make_mob("answerer", OTHER_SCRIPT_VNUM);
    obj_to_char(make_object(STONE_VNUM, 0), asker);

    run_command(do_say, "hello");

    EXPECT_TRUE(output_has("Holding a grey stone.")) << output();
}

/* ---- scripts paused with DO_WAIT ---- */

/* A script body that pauses for one pulse, then tells the room the mob fades and removes it. */
static std::vector<body_line> pause_then_remove_body()
{
    return { { SCRIPT_DO_WAIT, nullptr, { 1 } },
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

/* A higher-priority delay given to a paused mob (as bash gives its victim) drops the rest of the
 * paused script instead of running it in the middle of the bash. */
TEST_F(CharacterTriggerTest, ANewDelayOnAPausedMobDropsTheRestOfItsScript)
{
    install_script(ON_HEAR_SAY, pause_then_remove_body());
    char_data* mob = make_mob("mob", SCRIPT_VNUM);
    run_command(do_say, "hello");
    ASSERT_EQ(CMD_SCRIPT, mob->delay.cmd);

    WAIT_STATE_FULL(mob, 3, 0, 0, 50, 0, 0, 0, AFF_WAITING, TARGET_IGNORE);

    EXPECT_TRUE(is_in_game(mob));
    EXPECT_EQ(3, mob->delay.wait_value);
    for (int pulse = 0; pulse < 3; ++pulse) {
        process_waiting_list();
    }
    EXPECT_TRUE(is_in_game(mob)) << "the dropped script never resumes";
    EXPECT_FALSE(output_has("The mob fades."));
}

/* A paused mob that is killed dies without finishing its script. */
TEST_F(CharacterTriggerTest, APausedMobThatIsKilledDiesWithoutFinishingItsScript)
{
    install_script(ON_HEAR_SAY, pause_then_remove_body());
    char_data* mob = make_mob("mob", SCRIPT_VNUM);
    run_command(do_say, "hello");
    ASSERT_EQ(CMD_SCRIPT, mob->delay.cmd);
    /* make_corpse reads the mob's prototype entry, so the mob gets one; with a prototype the
     * game no longer frees its strings, so they are the test's. */
    index_data prototype_index[1] {};
    index_data* saved_mob_index = mob_index;
    mob_index = prototype_index;
    char mob_name[] = "mob";
    RELEASE(mob->player.name);
    RELEASE(mob->player.short_descr);
    mob->player.name = mob_name;
    mob->player.short_descr = mob_name;
    mob->nr = 0;

    raw_kill(mob, nullptr, 0);

    EXPECT_FALSE(is_in_game(mob));
    EXPECT_FALSE(output_has("The mob fades."));
    EXPECT_EQ(nullptr, waiting_list);
    mob_index = saved_mob_index;
}

/* ---- a mob's script set off again while it runs ---- */

/* The asker's hearing script says "ping"; the answerer's says "pong", which the asker hears
 * inside its first run. The nested run sees the int2 the outer run set and sets int1, which the
 * outer run then sees: the two share int1-int3, as one state always has. */
TEST_F(CharacterTriggerTest, ANestedRunSharesTheOuterRunsInts)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_HEAR_SAY,
                              { { SCRIPT_IF_STR_CONTAINS, "HELLO", { SCRIPT_PARAM_STR1 } },
                                  { SCRIPT_BEGIN, nullptr, {} },
                                  { SCRIPT_SET_INT_VALUE, nullptr, { 9, SCRIPT_PARAM_INT2 } },
                                  { SCRIPT_DO_SAY, "ping", { SCRIPT_PARAM_CH1 } },
                                  { SCRIPT_IF_INT_EQUAL, nullptr, { SCRIPT_PARAM_INT1, SCRIPT_PARAM_INT2 } },
                                  { SCRIPT_SEND_TO_CHAR, "The nested run's int1 came back.", { SCRIPT_PARAM_CH2 } },
                                  { SCRIPT_END, nullptr, {} },
                                  { SCRIPT_IF_STR_CONTAINS, "PONG", { SCRIPT_PARAM_STR1 } },
                                  { SCRIPT_BEGIN, nullptr, {} },
                                  { SCRIPT_SET_INT_VALUE, nullptr, { 9, SCRIPT_PARAM_INT3 } },
                                  { SCRIPT_IF_INT_EQUAL, nullptr, { SCRIPT_PARAM_INT2, SCRIPT_PARAM_INT3 } },
                                  { SCRIPT_SET_INT_VALUE, nullptr, { 9, SCRIPT_PARAM_INT1 } },
                                  { SCRIPT_END, nullptr, {} } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_HEAR_SAY,
                { { SCRIPT_IF_STR_CONTAINS, "PING", { SCRIPT_PARAM_STR1 } },
                    { SCRIPT_DO_SAY, "pong", { SCRIPT_PARAM_CH1 } } } } } } });
    make_mob("asker", SCRIPT_VNUM);
    make_mob("answerer", OTHER_SCRIPT_VNUM);

    run_command(do_say, "hello");

    EXPECT_TRUE(output_has("The nested run's int1 came back.")) << output();
}

/* The asker's nested run removes the asker; its first run goes on with what it has left. */
TEST_F(CharacterTriggerTest, AScriptRunsOnAfterANestedRunRemovesItsMob)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_HEAR_SAY,
                              { { SCRIPT_IF_STR_CONTAINS, "HELLO", { SCRIPT_PARAM_STR1 } },
                                  { SCRIPT_BEGIN, nullptr, {} },
                                  { SCRIPT_DO_SAY, "ping", { SCRIPT_PARAM_CH1 } },
                                  { SCRIPT_SEND_TO_CHAR, "Outer still running.", { SCRIPT_PARAM_CH2 } },
                                  { SCRIPT_END, nullptr, {} },
                                  { SCRIPT_IF_STR_CONTAINS, "PONG", { SCRIPT_PARAM_STR1 } },
                                  { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_HEAR_SAY,
                { { SCRIPT_IF_STR_CONTAINS, "PING", { SCRIPT_PARAM_STR1 } },
                    { SCRIPT_DO_SAY, "pong", { SCRIPT_PARAM_CH1 } } } } } } });
    char_data* asker = make_mob("asker", SCRIPT_VNUM);
    make_mob("answerer", OTHER_SCRIPT_VNUM);

    run_command(do_say, "hello");

    EXPECT_FALSE(is_in_game(asker));
    EXPECT_TRUE(output_has("Outer still running."));
}

/* A nested run that pauses leaves its resume point where the waiting list finds it. */
TEST_F(CharacterTriggerTest, ANestedRunThatPausesResumesFromTheWaitingList)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_HEAR_SAY,
                              { { SCRIPT_IF_STR_CONTAINS, "HELLO", { SCRIPT_PARAM_STR1 } },
                                  { SCRIPT_BEGIN, nullptr, {} },
                                  { SCRIPT_DO_SAY, "ping", { SCRIPT_PARAM_CH1 } },
                                  { SCRIPT_SEND_TO_CHAR, "Outer done.", { SCRIPT_PARAM_CH2 } },
                                  { SCRIPT_END, nullptr, {} },
                                  { SCRIPT_IF_STR_CONTAINS, "PONG", { SCRIPT_PARAM_STR1 } },
                                  { SCRIPT_BEGIN, nullptr, {} },
                                  { SCRIPT_DO_WAIT, nullptr, { 1 } },
                                  { SCRIPT_SEND_TO_ROOM, "Resumed.", { SCRIPT_PARAM_CH1_ROOM } },
                                  { SCRIPT_END, nullptr, {} } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_HEAR_SAY,
                { { SCRIPT_IF_STR_CONTAINS, "PING", { SCRIPT_PARAM_STR1 } },
                    { SCRIPT_DO_SAY, "pong", { SCRIPT_PARAM_CH1 } } } } } } });
    make_mob("asker", SCRIPT_VNUM);
    make_mob("answerer", OTHER_SCRIPT_VNUM);
    run_command(do_say, "hello");
    ASSERT_TRUE(output_has("Outer done."));
    ASSERT_FALSE(output_has("Resumed."));

    process_waiting_list();

    EXPECT_EQ(1, count_in_output("Resumed."));
}

/* Two mobs that answer whatever they hear would answer each other for ever: past
 * MAX_SCRIPT_NESTING runs inside one another the next is refused and logged. */
TEST_F(CharacterTriggerTest, ScriptsNestedTooDeeplyAreRefused)
{
    install_script(ON_HEAR_SAY, { { SCRIPT_DO_SAY, "again", { SCRIPT_PARAM_CH1 } } });
    char_data* speaker = make_mob("speaker", 0);
    char_data* first = make_mob("first", SCRIPT_VNUM);
    char_data* second = make_mob("second", SCRIPT_VNUM);
    world[0].people = m_player.next_in_room; /* nobody to send the endless talk to */

    testing::internal::CaptureStderr();
    run_command_as(speaker, do_say, "go");
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_NE(std::string::npos, logged.find("scripts are already running inside one another"));
    EXPECT_NE(std::string::npos, logged.find(std::to_string(MAX_SCRIPT_NESTING)));
    EXPECT_TRUE(is_in_game(first));
    EXPECT_TRUE(is_in_game(second));
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

/* ---- text variables ---- */

/* A hearing script keeps the text it heard across a pause. */
TEST_F(CharacterTriggerTest, AHearingScriptStillHasWhatItHeardAfterAPause)
{
    install_script(ON_HEAR_SAY,
        { { SCRIPT_DO_WAIT, nullptr, { 1 } },
            { SCRIPT_IF_STR_CONTAINS, "HELLO", { SCRIPT_PARAM_STR1 } },
            { SCRIPT_SEND_TO_ROOM, "Still heard.", { SCRIPT_PARAM_CH1_ROOM } } });
    make_mob("mob", SCRIPT_VNUM);
    run_command(do_say, "hello");

    process_waiting_list();

    EXPECT_TRUE(output_has("Still heard."));
}

/* A new run of a mob's scripts starts with no text from the last one. */
TEST_F(CharacterTriggerTest, ANewRunStartsWithoutTheLastRunsText)
{
    install_scripts({ { SCRIPT_VNUM,
        { { ON_HEAR_SAY, { { SCRIPT_SEND_TO_CHAR, SCRIPT_RAN_MESSAGE, { SCRIPT_PARAM_CH2 } } } },
            { ON_ENTER,
                { { SCRIPT_IF_STR_CONTAINS, "HELLO", { SCRIPT_PARAM_STR1 } },
                    { SCRIPT_SEND_TO_CHAR, "The old text is still there.", { SCRIPT_PARAM_CH2 } } } } } } });
    make_mob("mob", SCRIPT_VNUM);
    run_command(do_say, "hello");
    ASSERT_TRUE(output_has(SCRIPT_RAN_MESSAGE));

    call_trigger(ON_ENTER, &world[0], &m_player, 0);

    EXPECT_FALSE(output_has("The old text is still there."));
}

/* ---- mobs made from a prototype ---- */

/* A copy never shares its prototype's script state, so freeing it cannot free the prototype's. */
TEST_F(CharacterTriggerTest, AMobIsMadeWithoutItsPrototypesScriptState)
{
    char_data prototype {};
    clear_char(&prototype, MOB_ISNPC);
    SET_BIT(MOB_FLAGS(&prototype), MOB_ISNPC);
    char prototype_name[] = "copy";
    prototype.player.name = prototype_name;
    prototype.player.short_descr = prototype_name;
    prototype.abilities.hit = MOB_HIT_POINTS;
    prototype.tmpabilities.hit = MOB_HIT_POINTS;
    info_script prototype_state {};
    prototype.specials.script_info = &prototype_state;
    index_data prototype_index[1] {};
    char_data* saved_mob_proto = mob_proto;
    index_data* saved_mob_index = mob_index;
    const int saved_top_of_mobt = top_of_mobt;
    mob_proto = &prototype;
    mob_index = prototype_index;
    top_of_mobt = 0;

    char_data* copy = read_mobile(0, REAL);

    ASSERT_NE(nullptr, copy);
    EXPECT_EQ(nullptr, copy->specials.script_info);
    extract_char(copy);
    mob_proto = saved_mob_proto;
    mob_index = saved_mob_index;
    top_of_mobt = saved_top_of_mobt;
    RELEASE(prototype.profs);
}
