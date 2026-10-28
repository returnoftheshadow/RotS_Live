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
 * nobody is entering. */
TEST_F(CharacterTriggerTest, LaterMobsStillReactWhenAMobEnteringIsRemoved)
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

    EXPECT_EQ(1, call_trigger(ON_ENTER, &world[0], wanderer, 0));

    EXPECT_FALSE(is_in_game(wanderer));
    EXPECT_TRUE(output_has("The watcher looks up."));
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
