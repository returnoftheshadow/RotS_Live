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
