/* Moving, fleeing and fighting while a script, set off on the way, removes one of the characters
 * involved (EXTRACT_CHAR): whoever was moving, fleeing or fighting stops touching the removed one.
 *
 * The scripts here belong to mobs waiting in the second room or to the one being hit. A test
 * that needs a roll to go one way queues it with push_test_random_value. */
#include "scripted_room_fixture.h"

#include "../spells.h"

void do_move(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_flee(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_kick(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void hit(char_data* ch, char_data* victim, int type);
void perform_violence();
void set_fighting(char_data* ch, char_data* victim);
void on_windblast_success(char_data* ch, int mana_cost, int move_cost);
void die(char_data* dead_man, char_data* killer, int attack_type);

extern struct index_data* mob_index;

using namespace scripted_room;

namespace {

/* Enough movement points for any step. */
constexpr int PLENTY_OF_MOVES = 100;

class CombatMovementTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        link_second_room();
    }

    /* Makes script SCRIPT_VNUM a guard that removes whoever triggers <trigger_type> in its room
     * (ch2), and script OTHER_SCRIPT_VNUM a watcher that tells the room it saw someone. */
    void install_guard_and_watcher(int trigger_type)
    {
        install_scripts({ { SCRIPT_VNUM,
                              { { trigger_type, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } } } } },
            { OTHER_SCRIPT_VNUM,
                { { trigger_type,
                    { { SCRIPT_SEND_TO_ROOM, "The watcher looks up.", { SCRIPT_PARAM_CH1_ROOM } } } } } } });
    }

    /* A mob in room 0 with the moves to walk anywhere. */
    char_data* make_walker(const char* keywords)
    {
        char_data* walker = make_mob(keywords, 0);
        walker->abilities.move = PLENTY_OF_MOVES;
        walker->tmpabilities.move = PLENTY_OF_MOVES;
        return walker;
    }

    /* Seats <rider> on <mount>, behind any rider already there, as do_ride leaves them. */
    static void seat(char_data* rider, char_data* mount)
    {
        rider->mount_data.mount = mount;
        rider->mount_data.mount_number = mount->abs_number;
        rider->mount_data.next_rider = nullptr;
        rider->mount_data.next_rider_number = -1;
        if (!mount->mount_data.rider) {
            mount->mount_data.rider = rider;
            mount->mount_data.rider_number = rider->abs_number;
            return;
        }
        char_data* last = mount->mount_data.rider;
        while (last->mount_data.next_rider) {
            last = last->mount_data.next_rider;
        }
        last->mount_data.next_rider = rider;
        last->mount_data.next_rider_number = rider->abs_number;
    }

    /* Makes <victim> a wimpy mob badly hurt enough to flee from its next blow. */
    static void make_ready_to_flee(char_data* victim)
    {
        SET_BIT(MOB_FLAGS(victim), MOB_WIMPY);
        victim->abilities.move = PLENTY_OF_MOVES;
        victim->tmpabilities.move = PLENTY_OF_MOVES;
        victim->tmpabilities.hit = 1;
        victim->specials.position = POSITION_FIGHTING;
    }

    /* Moves <walker> north, as the interpreter does for the north command. */
    static void walk_north(char_data* walker) { do_move(walker, empty_argument(), nullptr, NORTH + 1, 0); }

    /* A writable empty argument for the commands. */
    static char* empty_argument()
    {
        static char empty[1] = "";
        empty[0] = '\0';
        return empty;
    }
};

} // namespace

/* ---- moving into a room whose mob removes the one arriving ---- */

TEST_F(CombatMovementTest, AWalkerRemovedBeforeItEntersNeverMoves)
{
    install_guard_and_watcher(ON_BEFORE_ENTER);
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    make_mob("watcher", OTHER_SCRIPT_VNUM, SECOND_ROOM);
    char_data* walker = make_walker("walker");

    walk_north(walker);

    EXPECT_FALSE(is_in_game(walker));
    EXPECT_FALSE(output_has("The watcher looks up."));
}

TEST_F(CombatMovementTest, AWalkerRemovedAsItEntersIsLetGo)
{
    install_guard_and_watcher(ON_ENTER);
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* walker = make_walker("walker");

    walk_north(walker);

    EXPECT_FALSE(is_in_game(walker));
}

TEST_F(CombatMovementTest, AWalkerTheRoomKeepsArrives)
{
    install_script(ON_ENTER, { { SCRIPT_SEND_TO_ROOM, "The watcher looks up.", { SCRIPT_PARAM_CH1_ROOM } } });
    make_mob("watcher", SCRIPT_VNUM, SECOND_ROOM);
    char_data* walker = make_walker("walker");

    walk_north(walker);

    EXPECT_TRUE(is_in_game(walker));
    EXPECT_EQ(SECOND_ROOM, walker->in_room);
}

/* The guard removes only the passenger, the second rider: the mount's own check of its riders
 * calls the move off and touches the passenger no more. */
TEST_F(CombatMovementTest, AMountWhosePassengerIsRemovedBeforeEnteringStays)
{
    install_scripts({ { SCRIPT_VNUM,
        { { ON_BEFORE_ENTER,
            { { SCRIPT_IF_STR_EQUAL, "passenger", { SCRIPT_PARAM_CH2_NAME } },
                { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } } } } } });
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* mount = make_walker("horse");
    char_data* rider = make_walker("rider");
    char_data* passenger = make_walker("passenger");
    seat(rider, mount);
    seat(passenger, mount);

    walk_north(rider);

    EXPECT_FALSE(is_in_game(passenger));
    EXPECT_EQ(0, mount->in_room);
    EXPECT_EQ(0, rider->in_room);
}

/* Every rider and the mount are removed as they arrive. */
TEST_F(CombatMovementTest, AMountAndRidersRemovedAsTheyArriveAreLetGo)
{
    install_guard_and_watcher(ON_ENTER);
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* mount = make_walker("horse");
    char_data* rider = make_walker("rider");
    char_data* passenger = make_walker("passenger");
    seat(rider, mount);
    seat(passenger, mount);

    walk_north(rider);

    EXPECT_FALSE(is_in_game(rider));
    EXPECT_FALSE(is_in_game(passenger));
    EXPECT_FALSE(is_in_game(mount));
}

/* ---- fleeing ---- */

TEST_F(CombatMovementTest, AFleerRemovedBeforeItEntersIsLetGo)
{
    install_guard_and_watcher(ON_BEFORE_ENTER);
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* fleer = make_walker("fleer");
    fleer->specials.position = POSITION_FIGHTING;

    do_flee(fleer, empty_argument(), nullptr, 0, 0);

    EXPECT_FALSE(is_in_game(fleer));
}

/* The wind pushes the victim out, where it is removed: the caster does not go on to hurt it. */
TEST_F(CombatMovementTest, AWindblastVictimRemovedWhereItLandsIsNotHurt)
{
    install_guard_and_watcher(ON_BEFORE_ENTER);
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* caster = make_walker("caster");
    char_data* first_victim = make_walker("first");
    char_data* second_victim = make_walker("second");
    second_victim->tmpabilities.dex = 100; /* it always saves, and is only hurt */
    /* The player stands aside: pushing a player out would move its race's power between zones. */
    world[0].people = m_player.next_in_room;
    push_test_random_value(0.0); /* the damage roll */
    push_test_random_value(0.99); /* the first victim's save: it fails and is pushed */

    on_windblast_success(caster, 0, 0);

    EXPECT_TRUE(is_in_game(caster));
    EXPECT_FALSE(is_in_game(first_victim));
    EXPECT_TRUE(is_in_game(second_victim)) << "the blast went on to the next one";
}

/* ---- blows ---- */

/* Too tired to strike, the attacker still frightens its badly hurt victim into fleeing; the
 * victim is removed where it flees to, and the attack lets it go. */
TEST_F(CombatMovementTest, AMissThatMakesTheVictimFleeLetsGoOfARemovedVictim)
{
    install_guard_and_watcher(ON_BEFORE_ENTER);
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* attacker = make_walker("attacker");
    char_data* victim = make_walker("victim");
    make_ready_to_flee(victim);
    attacker->specials.position = POSITION_FIGHTING;
    attacker->specials.ENERGY = 0;

    hit(attacker, victim, TYPE_UNDEFINED);

    EXPECT_FALSE(is_in_game(victim));
    EXPECT_TRUE(is_in_game(attacker));
}

/* The victim's damage script removes its attacker: the combat round goes on without it. */
TEST_F(CombatMovementTest, TheCombatRoundLetsGoOfAFighterItsOwnBlowRemoved)
{
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    char_data* fighter = make_walker("fighter");
    char_data* victim = make_walker("victim");
    victim->specials.script_number = SCRIPT_VNUM;
    fighter->specials.position = POSITION_FIGHTING;
    set_fighting(fighter, victim);
    fighter->specials.ENERGY = ENE_TO_HIT * 2;
    push_test_random_value(0.999); /* the to-hit roll: a sure hit */

    perform_violence();

    EXPECT_FALSE(is_in_game(fighter));
    EXPECT_TRUE(is_in_game(victim));
}

TEST_F(CombatMovementTest, AKickerTheVictimsScriptRemovesHasNoRecovery)
{
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    char_data* kicker = make_walker("kicker");
    char_data* victim = make_walker("victim");
    victim->specials.script_number = SCRIPT_VNUM;
    char argument[] = "victim";

    do_kick(kicker, argument, nullptr, 0, SCMD_KICK);

    EXPECT_FALSE(is_in_game(kicker));
    EXPECT_TRUE(is_in_game(victim));
}

/* The dying mob's last words make its killer, a mob, hear them and remove itself: the death goes
 * on without a killer. */
TEST_F(CombatMovementTest, ADeathGoesOnWithoutAKillerSomethingRemoved)
{
    install_scripts({ { SCRIPT_VNUM, { { ON_DIE, { { SCRIPT_DO_SAY, "avenge me", { SCRIPT_PARAM_CH1 } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_HEAR_SAY, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } } } } } });
    char_data* killer = make_walker("killer");
    killer->specials.script_number = OTHER_SCRIPT_VNUM;
    char_data* dying = make_walker("dying");
    dying->specials.script_number = SCRIPT_VNUM;
    /* make_corpse reads the dying mob's prototype entry, so it gets one; with a prototype the
     * game no longer frees its strings, so they are the test's. */
    index_data prototype_index[1] {};
    index_data* saved_mob_index = mob_index;
    mob_index = prototype_index;
    char dying_name[] = "dying";
    RELEASE(dying->player.name);
    RELEASE(dying->player.short_descr);
    dying->player.name = dying_name;
    dying->player.short_descr = dying_name;
    dying->nr = 0;

    die(dying, killer, 0);

    EXPECT_FALSE(is_in_game(killer));
    EXPECT_FALSE(is_in_game(dying));
    EXPECT_NE(nullptr, world[0].contents) << "the death went on and left a corpse";
    mob_index = saved_mob_index;
}
