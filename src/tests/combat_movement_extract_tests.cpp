/* Moving, fleeing and fighting while a script, set off on the way, removes one of the characters
 * involved (EXTRACT_CHAR): whoever was moving, fleeing or fighting stops touching the removed one.
 *
 * The scripts here belong to mobs waiting in the second room or to the one being hit. A test
 * that needs a roll to go one way queues it with push_test_random_value. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../skill_timer.h"
#include "../spells.h"

void do_move(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_flee(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_kick(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_say(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void hit(char_data* ch, char_data* victim, int type);
void perform_violence();
void set_fighting(char_data* ch, char_data* victim);
void on_windblast_success(char_data* ch, int mana_cost, int move_cost);
void die(char_data* dead_man, char_data* killer, int attack_type);
void do_rend(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_bite(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_maul(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_cleave(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_stomp(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_smash(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_bash(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_rescue(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_disengage(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_track(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void affect_update_person(struct char_data* i, int mode);
void assign_command_pointers(void);
SPECIAL(thuringwethil);

extern struct index_data* mob_index;
extern int top_of_mobt;
extern struct weather_data weather_info;
extern struct txt_block* txt_block_pool;

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
        /* Made at boot; the skills ask them. Making them again keeps the ones there are. */
        game_rules::big_brother::create(weather_info, &world);
        game_timer::skill_timer::create(weather_info, &world);
    }

    void TearDown() override
    {
        ScriptedRoomFixture::TearDown();
        if (m_saved_mob_index != nullptr) {
            mob_index = m_saved_mob_index;
        }
        RELEASE(m_dying_profs);
    }

    /* Gives <dying> the one prototype entry, which make_corpse reads. With a prototype the game
     * no longer frees the mob's strings or profession block, so the fixture owns them. */
    void give_prototype(char_data* dying)
    {
        m_saved_mob_index = mob_index;
        mob_index = m_prototype_index;
        RELEASE(dying->player.name);
        RELEASE(dying->player.short_descr);
        dying->player.name = m_dying_name;
        dying->player.short_descr = m_dying_name;
        m_dying_profs = dying->profs;
        dying->nr = 0;
    }

    /* Makes script SCRIPT_VNUM say "avenge me" as its mob dies, and script OTHER_SCRIPT_VNUM
     * remove its own mob when it hears anything. */
    void install_avenge_me_scripts()
    {
        const body_line last_words = { SCRIPT_DO_SAY, "avenge me", { SCRIPT_PARAM_CH1 } };
        const body_line vanish = { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } };
        install_scripts({ { SCRIPT_VNUM, { { ON_DIE, { last_words } } } },
            { OTHER_SCRIPT_VNUM, { { ON_HEAR_SAY, { vanish } } } } });
    }

    /* Gives <owner> a bag holding a ring, and returns the ring. */
    obj_data* give_ring_in_bag(char_data* owner)
    {
        obj_data* bag = make_object(BAG_VNUM, 0);
        obj_data* ring = make_object(RING_VNUM, 0);
        obj_to_obj(ring, bag);
        obj_to_char(bag, owner);
        return ring;
    }

    // The mob table give_prototype points the game at; its one entry is the dying mob's.
    index_data m_prototype_index[1] {};
    // The game's mob table while give_prototype has replaced it; null when it has not.
    index_data* m_saved_mob_index = nullptr;
    // The dying mob's name and short description once give_prototype has made them the test's.
    char m_dying_name[6] = "dying";
    // The dying mob's profession block; TearDown frees it, as its prototype keeps the game off it.
    char_prof_data* m_dying_profs = nullptr;

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

    /* Makes script SCRIPT_VNUM pause on hearing anything, then say "vanish", and script
     * OTHER_SCRIPT_VNUM remove its own mob when it hears that word. The rest of a paused script
     * names nobody but its mob, so it removes someone else through what it sets off, as here. */
    void install_pause_then_vanish_scripts()
    {
        install_scripts({ { SCRIPT_VNUM,
                              { { ON_HEAR_SAY,
                                  { { SCRIPT_DO_WAIT, nullptr, { 5 } },
                                      { SCRIPT_DO_SAY, "vanish", { SCRIPT_PARAM_CH1 } } } } } },
            { OTHER_SCRIPT_VNUM,
                { { ON_HEAR_SAY,
                    { { SCRIPT_IF_STR_CONTAINS, "VANISH", { SCRIPT_PARAM_STR1 } },
                        { SCRIPT_BEGIN, nullptr, {} },
                        { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } },
                        { SCRIPT_END, nullptr, {} } } } } } });
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

/* The mounted leader's move is called off when the passenger is removed before entering: the
 * leader's follower does not go either, as for a walker whose move fails. */
TEST_F(CombatMovementTest, AFollowerStaysWhenItsMountedLeadersMoveIsCalledOff)
{
    assign_command_pointers();
    install_scripts({ { SCRIPT_VNUM,
        { { ON_BEFORE_ENTER,
            { { SCRIPT_IF_STR_EQUAL, "passenger", { SCRIPT_PARAM_CH2_NAME } },
                { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } } } } } });
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* mount = make_walker("horse");
    char_data* rider = make_walker("rider");
    char_data* passenger = make_walker("passenger");
    char_data* follower = make_walker("follower");
    seat(rider, mount);
    seat(passenger, mount);
    add_follower(follower, rider, FOLLOW_MOVE);

    walk_north(rider);

    EXPECT_FALSE(is_in_game(passenger));
    EXPECT_EQ(0, rider->in_room);
    ASSERT_TRUE(is_in_game(follower));
    EXPECT_EQ(0, follower->in_room);
}

/* A mounted leader that rides on is followed. */
TEST_F(CombatMovementTest, AFollowerFollowsItsMountedLeader)
{
    assign_command_pointers();
    char_data* mount = make_walker("horse");
    char_data* rider = make_walker("rider");
    char_data* follower = make_walker("follower");
    seat(rider, mount);
    add_follower(follower, rider, FOLLOW_MOVE);

    walk_north(rider);

    EXPECT_EQ(SECOND_ROOM, mount->in_room);
    EXPECT_EQ(SECOND_ROOM, rider->in_room);
    EXPECT_EQ(SECOND_ROOM, follower->in_room);
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
    second_victim->tmpabilities.dex = 120; /* it saves, and is only hurt */
    /* The player stands aside: pushing a player out would move its race's power between zones. */
    world[0].people = m_player.next_in_room;
    push_test_random_value(0.0); /* the damage roll */
    push_test_random_value(0.99); /* the first victim's save: it fails and is pushed */
    push_test_random_value(0.0); /* the direction it is pushed */
    push_test_random_value(0.0); /* the second victim's save */

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
    install_avenge_me_scripts();
    char_data* killer = make_walker("killer");
    killer->specials.script_number = OTHER_SCRIPT_VNUM;
    char_data* dying = make_walker("dying");
    dying->specials.script_number = SCRIPT_VNUM;
    give_prototype(dying);

    die(dying, killer, 0);

    EXPECT_FALSE(is_in_game(killer));
    EXPECT_FALSE(is_in_game(dying));
    EXPECT_NE(nullptr, world[0].contents) << "the death went on and left a corpse";
}

/* A mob slain by a mob keeps the wearables in its containers where they are, even when its death
 * script removes the killer before the corpse is made. */
TEST_F(CombatMovementTest, AMobsCorpseLeavesItsBagsShutWhenItsMobKillerIsRemoved)
{
    install_avenge_me_scripts();
    char_data* killer = make_walker("killer");
    killer->specials.script_number = OTHER_SCRIPT_VNUM;
    char_data* dying = make_walker("dying");
    dying->specials.script_number = SCRIPT_VNUM;
    give_prototype(dying);
    obj_data* ring = give_ring_in_bag(dying);
    obj_data* bag = ring->in_obj;

    die(dying, killer, 0);

    ASSERT_FALSE(is_in_game(killer));
    EXPECT_EQ(bag, ring->in_obj) << "the ring stays in its bag";
}

/* A death with no killer at all takes the wearables out of the dead one's containers into the
 * corpse. */
TEST_F(CombatMovementTest, ADeathWithNoKillerGathersWearablesIntoTheCorpse)
{
    char_data* dying = make_walker("dying");
    give_prototype(dying);
    obj_data* ring = give_ring_in_bag(dying);
    obj_data* bag = ring->in_obj;

    die(dying, nullptr, 0);

    ASSERT_NE(nullptr, bag->in_obj) << "the bag is in the corpse";
    EXPECT_EQ(bag->in_obj, ring->in_obj) << "the ring is taken out into the corpse";
}

/* ---- scripts refused past the limits ---- */

/* The asker answers "go" by saying "x1", which two mobs answer with two says each, without end,
 * using up the event's scripts; then the asker flees. The guard's ON_BEFORE_ENTER script, refused,
 * keeps the asker out. */
static std::vector<script_spec> exhaust_then_flee_scripts()
{
    const std::vector<body_line> answer = { { SCRIPT_IF_STR_CONTAINS, "X", { SCRIPT_PARAM_STR1 } },
        { SCRIPT_BEGIN, nullptr, {} }, { SCRIPT_DO_SAY, "x1", { SCRIPT_PARAM_CH1 } },
        { SCRIPT_DO_SAY, "x2", { SCRIPT_PARAM_CH1 } }, { SCRIPT_END, nullptr, {} } };
    return { { SCRIPT_VNUM,
                 { { ON_HEAR_SAY,
                     { { SCRIPT_IF_STR_CONTAINS, "GO", { SCRIPT_PARAM_STR1 } },
                         { SCRIPT_BEGIN, nullptr, {} },
                         { SCRIPT_DO_SAY, "x1", { SCRIPT_PARAM_CH1 } },
                         { SCRIPT_DO_FLEE, nullptr, { SCRIPT_PARAM_CH1 } },
                         { SCRIPT_END, nullptr, {} } } } } },
        { OTHER_SCRIPT_VNUM, { { ON_HEAR_SAY, answer } } },
        { THIRD_SCRIPT_VNUM,
            { { ON_BEFORE_ENTER,
                { { SCRIPT_SEND_TO_ROOM, "The guard checks.", { SCRIPT_PARAM_CH1_ROOM } } } } } } };
}

TEST_F(CombatMovementTest, AGuardWhoseScriptIsRefusedKeepsTheDoorShut)
{
    install_scripts(exhaust_then_flee_scripts());
    char_data* speaker = make_walker("speaker");
    char_data* asker = make_walker("asker");
    asker->specials.script_number = SCRIPT_VNUM;
    make_mob("first answerer", OTHER_SCRIPT_VNUM);
    make_mob("second answerer", OTHER_SCRIPT_VNUM);
    make_mob("guard", THIRD_SCRIPT_VNUM, SECOND_ROOM);
    world[0].people = m_player.next_in_room; /* the player stands aside */
    char go[] = "go";

    testing::internal::CaptureStderr();
    do_say(speaker, go, nullptr, 0, 0);
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(0, asker->in_room) << "refused, the guard keeps the asker out";
}

/* The same without the answerers: the guard's script runs and lets the asker through. */
TEST_F(CombatMovementTest, AGuardWhoseScriptRunsLetsTheFleerIn)
{
    install_scripts(exhaust_then_flee_scripts());
    char_data* speaker = make_walker("speaker");
    char_data* asker = make_walker("asker");
    asker->specials.script_number = SCRIPT_VNUM;
    make_mob("guard", THIRD_SCRIPT_VNUM, SECOND_ROOM);
    world[0].people = m_player.next_in_room;
    char go[] = "go";

    do_say(speaker, go, nullptr, 0, 0);

    EXPECT_EQ(SECOND_ROOM, asker->in_room);
}

/* Scripts for a chain of runs inside one another as deep as runs may go. Script SCRIPT_VNUM answers
 * whatever it hears with "pong", and tells the room "Ouch." when it is hurt. Script
 * OTHER_SCRIPT_VNUM counts its runs in int1 and answers with "ping", but on run
 * <acting_run> does <action> instead. Script THIRD_SCRIPT_VNUM is a guard that checks whoever
 * enters its room.
 *
 * The pong mob, made first, hears the player first, so the ping mob's run n is at depth 2n: its
 * run 128 is at MAX_SCRIPT_NESTING, where no further run may start, and its run 127 two below. */
static std::vector<script_spec> deepest_run_scripts(int acting_run, const body_line& action)
{
    return { { SCRIPT_VNUM,
                 { { ON_HEAR_SAY, { { SCRIPT_DO_SAY, "pong", { SCRIPT_PARAM_CH1 } } } },
                     { ON_DAMAGE, { { SCRIPT_SEND_TO_ROOM, "Ouch.", { SCRIPT_PARAM_CH1_ROOM } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_HEAR_SAY,
                { { SCRIPT_SET_INT_VALUE, nullptr, { 1, SCRIPT_PARAM_INT2 } },
                    { SCRIPT_SET_INT_SUM, nullptr,
                        { SCRIPT_PARAM_INT1, SCRIPT_PARAM_INT1, SCRIPT_PARAM_INT2 } },
                    { SCRIPT_SET_INT_VALUE, nullptr, { acting_run, SCRIPT_PARAM_INT3 } },
                    { SCRIPT_IF_INT_EQUAL, nullptr, { SCRIPT_PARAM_INT1, SCRIPT_PARAM_INT3 } },
                    { SCRIPT_BEGIN, nullptr, {} }, action, { SCRIPT_END_ELSE_BEGIN, nullptr, {} },
                    { SCRIPT_DO_SAY, "ping", { SCRIPT_PARAM_CH1 } }, { SCRIPT_END, nullptr, {} } } } } },
        { THIRD_SCRIPT_VNUM,
            { { ON_BEFORE_ENTER,
                { { SCRIPT_SEND_TO_ROOM, "The guard checks.", { SCRIPT_PARAM_CH1_ROOM } } } } } } };
}

/* The run the ping mob makes at the deepest a run may go. */
constexpr int DEEPEST_PING_RUN = MAX_SCRIPT_NESTING / 2;

/* The ping mob flees at the deepest run: the guard's script cannot run, so the guard keeps it out
 * (a refused ON_BEFORE_ENTER fails closed). */
TEST_F(CombatMovementTest, AGuardWhoseScriptIsTooDeepKeepsTheDoorShut)
{
    install_scripts(
        deepest_run_scripts(DEEPEST_PING_RUN, { SCRIPT_DO_FLEE, nullptr, { SCRIPT_PARAM_CH1 } }));
    char_data* ponger = make_walker("ponger");
    ponger->specials.script_number = SCRIPT_VNUM;
    char_data* pinger = make_walker("pinger");
    pinger->specials.script_number = OTHER_SCRIPT_VNUM;
    make_mob("guard", THIRD_SCRIPT_VNUM, SECOND_ROOM);

    testing::internal::CaptureStderr();
    run_command(do_say, "go");
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(0, pinger->in_room) << "refused, the guard keeps the pinger out";
    EXPECT_NE(std::string::npos, logged.find("script #9952 not run: too many scripts are running"))
        << logged;
}

/* Two runs shallower the guard's script runs and lets the pinger in. */
TEST_F(CombatMovementTest, AGuardWhoseScriptIsJustDeepEnoughLetsTheFleerIn)
{
    install_scripts(
        deepest_run_scripts(DEEPEST_PING_RUN - 1, { SCRIPT_DO_FLEE, nullptr, { SCRIPT_PARAM_CH1 } }));
    char_data* ponger = make_walker("ponger");
    ponger->specials.script_number = SCRIPT_VNUM;
    char_data* pinger = make_walker("pinger");
    pinger->specials.script_number = OTHER_SCRIPT_VNUM;
    make_mob("guard", THIRD_SCRIPT_VNUM, SECOND_ROOM);

    testing::internal::CaptureStderr();
    run_command(do_say, "go");
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(SECOND_ROOM, pinger->in_room);
}

/* The ping mob hits the pong mob at the deepest run: the pong mob's damage script cannot run, so
 * the blow is not dealt (a refused ON_DAMAGE fails closed). */
TEST_F(CombatMovementTest, ABlowWhoseDamageScriptIsTooDeepIsNotDealt)
{
    install_scripts(deepest_run_scripts(
        DEEPEST_PING_RUN, { SCRIPT_DO_HIT, nullptr, { SCRIPT_PARAM_CH1, SCRIPT_PARAM_CH2 } }));
    char_data* ponger = make_walker("ponger");
    ponger->specials.script_number = SCRIPT_VNUM;
    char_data* pinger = make_walker("pinger");
    pinger->specials.script_number = OTHER_SCRIPT_VNUM;
    pinger->points.OB = 200; /* sure to land */
    pinger->specials.ENERGY = ENE_TO_HIT * 2; /* ready to strike */
    equip_char(pinger, make_object(SWORD_VNUM, 0), WIELD); /* a blow that hurts */

    testing::internal::CaptureStderr();
    run_command(do_say, "go");
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(0, count_in_output("Ouch."));
    EXPECT_EQ(ponger->abilities.hit, ponger->tmpabilities.hit);
}

/* Two runs shallower the damage script runs, and the blow is dealt. */
TEST_F(CombatMovementTest, ABlowWhoseDamageScriptIsJustDeepEnoughIsDealt)
{
    install_scripts(deepest_run_scripts(
        DEEPEST_PING_RUN - 1, { SCRIPT_DO_HIT, nullptr, { SCRIPT_PARAM_CH1, SCRIPT_PARAM_CH2 } }));
    char_data* ponger = make_walker("ponger");
    ponger->specials.script_number = SCRIPT_VNUM;
    char_data* pinger = make_walker("pinger");
    pinger->specials.script_number = OTHER_SCRIPT_VNUM;
    pinger->points.OB = 200; /* sure to land */
    pinger->specials.ENERGY = ENE_TO_HIT * 2; /* ready to strike */
    equip_char(pinger, make_object(SWORD_VNUM, 0), WIELD); /* a blow that hurts */

    testing::internal::CaptureStderr();
    run_command(do_say, "go");
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(1, count_in_output("Ouch."));
    EXPECT_LT(ponger->tmpabilities.hit, ponger->abilities.hit);
}

/* ---- skills and special procedures ---- */

/* Makes the player an olog-hai who knows its skills and wields a sword (a mob olog-hai cannot use
 * them: its damage divides by its tactics, which a mob does not have). */
static void make_olog_hai(char_data& player, obj_data* sword)
{
    /* The skill timers outlive a test and are kept by id: each test's olog-hai is someone new. */
    static long next_idnum = 900000;
    player.specials2.idnum = next_idnum++;
    player.player.race = RACE_OLOGHAI;
    for (int skill : { SKILL_SMASH, SKILL_STOMP, SKILL_CLEAVE }) {
        player.knowledge[skill] = 80;
        player.skills[skill] = 80;
    }
    equip_char(&player, sword, WIELD);
    player.points.OB = 200; /* sure to land */
}

/* Makes script SCRIPT_VNUM a damage script that removes the attacker (ch2). */
static std::vector<body_line> remove_the_attacker_body()
{
    return { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } };
}

TEST_F(CombatMovementTest, ARendWhoseBlowRemovesTheBearStopsThere)
{
    install_script(ON_DAMAGE, remove_the_attacker_body());
    char_data* bear = make_walker("bear");
    bear->player.race = RACE_BEORNING;
    char_data* victim = make_walker("victim");
    victim->specials.script_number = SCRIPT_VNUM;
    waiting_type target {};
    target.targ1.set_character(victim);

    SET_BIT(victim->specials.affected_by, AFF_BASH); /* only a bashed victim is rent */

    do_rend(bear, empty_argument(), &target, 0, 0);

    EXPECT_FALSE(is_in_game(bear));
    EXPECT_TRUE(is_in_game(victim));
}

TEST_F(CombatMovementTest, ABiteWhoseBlowRemovesTheBearStopsThere)
{
    install_script(ON_DAMAGE, remove_the_attacker_body());
    char_data* bear = make_walker("bear");
    bear->player.race = RACE_BEORNING;
    char_data* victim = make_walker("victim");
    victim->specials.script_number = SCRIPT_VNUM;
    waiting_type target {};
    target.targ1.set_character(victim);

    do_bite(bear, empty_argument(), &target, 0, 0);

    EXPECT_FALSE(is_in_game(bear));
}

TEST_F(CombatMovementTest, AMaulWhoseBlowRemovesTheBearStopsThere)
{
    install_script(ON_DAMAGE, remove_the_attacker_body());
    char_data* bear = make_walker("bear");
    bear->player.race = RACE_BEORNING;
    char_data* victim = make_walker("victim");
    victim->specials.script_number = SCRIPT_VNUM;
    waiting_type target {};
    target.targ1.set_character(victim);

    do_maul(bear, empty_argument(), &target, 0, 0);

    EXPECT_FALSE(is_in_game(bear));
}

/* The olog-hai's cleave hits everyone in the room; each victim's damage script removes it, and the
 * cleave still goes on to the next. */
TEST_F(CombatMovementTest, ACleaveGoesOnPastVictimsThatRemoveThemselves)
{
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    make_olog_hai(m_player, make_object(SWORD_VNUM, 0));
    char_data* first = make_walker("first");
    first->specials.script_number = SCRIPT_VNUM;
    char_data* second = make_walker("second");
    second->specials.script_number = SCRIPT_VNUM;

    do_cleave(&m_player, empty_argument(), nullptr, 0, 0);

    EXPECT_FALSE(is_in_game(first));
    EXPECT_FALSE(is_in_game(second));
}

/* Stomp knocks its victim down before hurting it; the victim was paused in a script whose rest,
 * run at once by the knock-down, removes it: the stomp does not hurt what is gone. */
TEST_F(CombatMovementTest, AStompOnAPausedVictimItsScriptRemovesHurtsNobody)
{
    install_script(ON_HEAR_SAY,
        { { SCRIPT_DO_WAIT, nullptr, { 5 } }, { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    make_olog_hai(m_player, make_object(SWORD_VNUM, 0));
    char_data* victim = make_walker("victim");
    victim->specials.script_number = SCRIPT_VNUM;
    run_command(do_say, "hello");
    ASSERT_EQ(CMD_SCRIPT, victim->delay.cmd);
    push_test_random_value(0.99); /* the stomp's to-hit roll: it lands */

    do_stomp(&m_player, empty_argument(), nullptr, 0, 0);

    EXPECT_FALSE(is_in_game(victim)) << output();
}

/* A smash fells a mounted victim whose death script refuses death: the victim is still there, so
 * it is knocked off its mount as any smash of a rider does. */
TEST_F(CombatMovementTest, ASmashOnARiderWhoRefusesToDieStillUnseatsIt)
{
    install_script(ON_DIE, { { SCRIPT_RETURN_FALSE, nullptr, {} } });
    make_olog_hai(m_player, make_object(SWORD_VNUM, 0));
    char_data* rider = make_walker("rider");
    rider->specials.script_number = SCRIPT_VNUM;
    rider->tmpabilities.hit = 1;
    char_data* horse = make_walker("horse");
    seat(rider, horse);
    waiting_type target {};
    target.targ1.set_character(rider);
    push_test_random_value(0.99); /* the to-hit roll: it lands */
    push_test_random_value(0.99); /* the smash knocks a rider off */

    do_smash(&m_player, empty_argument(), &target, 0, 0);

    ASSERT_TRUE(is_in_game(rider));
    EXPECT_FALSE(IS_RIDING(rider)) << output();
}

/* Fear makes a mob flee into a room whose guard removes it: the fear does not go on with it. */
TEST_F(CombatMovementTest, AMobFearDrivesIntoItsRemovalIsLetGo)
{
    install_scripts({ { SCRIPT_VNUM,
        { { ON_BEFORE_ENTER, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } } } } } });
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* frightened = make_walker("frightened");
    frightened->specials.position = POSITION_FIGHTING;
    affected_type fear {};
    fear.type = SPELL_FEAR;
    fear.duration = 5;
    fear.location = APPLY_NONE;
    affect_to_char(frightened, &fear);

    affect_update_person(frightened, 0);

    EXPECT_FALSE(is_in_game(frightened));
}

/* Thuringwethil's procedure has her leave once she is down to 500 hit points, whatever event
 * calls it: a blow at her through damage() lets go of her when she does. */
TEST_F(CombatMovementTest, ABlowAtThuringwethilLetsGoOfHerWhenSheLeaves)
{
    char_data* attacker = make_walker("attacker");
    char_data* vampire = make_walker("thuringwethil");
    index_data prototype_index[1] {};
    prototype_index[0].func = thuringwethil;
    index_data* saved_mob_index = mob_index;
    const int saved_top_of_mobt = top_of_mobt;
    mob_index = prototype_index;
    top_of_mobt = 0;
    vampire->nr = 0;
    char vampire_name[] = "thuringwethil";
    RELEASE(vampire->player.name);
    RELEASE(vampire->player.short_descr);
    vampire->player.name = vampire_name;
    vampire->player.short_descr = vampire_name;
    char_prof_data* vampire_profs = vampire->profs; /* free_char leaves it to the prototype */
    SET_BIT(MOB_FLAGS(vampire), MOB_SPEC);
    vampire->tmpabilities.hit = 400;

    EXPECT_EQ(1, damage(attacker, vampire, 10, TYPE_HIT, 0));

    EXPECT_FALSE(is_in_game(vampire));
    EXPECT_TRUE(is_in_game(attacker));
    mob_index = saved_mob_index;
    top_of_mobt = saved_top_of_mobt;
    RELEASE(vampire_profs);
}

/* ---- delays that run the rest of a paused script ---- */


/* A bash knocks its victim down, which ends the victim's pause: the rest of its script makes the
 * basher vanish, and the basher then does not go on to hurt anyone. */
TEST_F(CombatMovementTest, ABasherTheBashedMobsPausedScriptRemovesStopsThere)
{
    install_pause_then_vanish_scripts();
    char_data* victim = make_walker("victim");
    victim->specials.script_number = SCRIPT_VNUM;
    char_data* basher = make_walker("basher");
    basher->specials.script_number = OTHER_SCRIPT_VNUM;
    basher->points.OB = 200; /* sure to land; a mob knows every skill at 80 */
    run_command_as(basher, do_say, "hello");
    ASSERT_EQ(CMD_SCRIPT, victim->delay.cmd);
    waiting_type target {};
    target.targ1.set_character(victim);

    do_bash(basher, empty_argument(), &target, CMD_BASH, 1);

    EXPECT_FALSE(is_in_game(basher));
    EXPECT_TRUE(is_in_game(victim));
    EXPECT_EQ(victim->tmpabilities.hit, victim->abilities.hit) << "nobody was left to hurt it";
}

/* A rescue holds up the one rescued, which ends its pause: the rest of its script makes the
 * rescuer vanish, and the rescuer is then not held up in turn. */
TEST_F(CombatMovementTest, ARescuerTheRescuedMobsPausedScriptRemovesStopsThere)
{
    install_pause_then_vanish_scripts();
    char_data* rescued = make_walker("rescued");
    rescued->specials.script_number = SCRIPT_VNUM;
    char_data* attacker = make_walker("attacker");
    char_data* rescuer = make_walker("rescuer");
    rescuer->specials.script_number = OTHER_SCRIPT_VNUM;
    run_command_as(rescuer, do_say, "hello");
    ASSERT_EQ(CMD_SCRIPT, rescued->delay.cmd);
    set_fighting(attacker, rescued);
    set_fighting(rescued, attacker);
    waiting_type target {};
    target.targ1.set_character(rescued);
    push_test_random_value(0.0); /* the rescue succeeds (a mob knows it at 80) */
    char rescued_name[] = "rescued";

    do_rescue(rescuer, rescued_name, &target, CMD_RESCUE, 0);

    EXPECT_FALSE(is_in_game(rescuer)) << output();
    EXPECT_TRUE(is_in_game(rescued));
}

/* Disengaging ends the disengager's pause: the rest of its script makes the one it fights vanish,
 * which ends the fight, and there is nobody left to stop fighting. */
TEST_F(CombatMovementTest, ADisengageWhosePausedScriptEndsTheFightStopsThere)
{
    install_pause_then_vanish_scripts();
    char_data* disengager = make_walker("disengager");
    disengager->specials.script_number = SCRIPT_VNUM;
    char_data* foe = make_walker("foe");
    foe->specials.script_number = OTHER_SCRIPT_VNUM;
    run_command(do_say, "hello");
    ASSERT_EQ(CMD_SCRIPT, disengager->delay.cmd);
    set_fighting(disengager, foe); /* the foe does not fight back, so it lets the disengager go */

    do_disengage(disengager, empty_argument(), nullptr, 0, 0);

    EXPECT_FALSE(is_in_game(foe));
    ASSERT_TRUE(is_in_game(disengager));
    EXPECT_EQ(nullptr, disengager->specials.fighting);
    EXPECT_EQ(0, count_in_output("stops fighting"));
}

/* Tracking while a delay that outranks it runs is a double delay: tracking does not start, and the
 * text block it took for the name to track goes back to the pool. */
TEST_F(CombatMovementTest, ATrackThatCannotStartGivesBackItsTextBlock)
{
    m_player.delay.wait_value = 5; /* a delay that outranks tracking */
    m_player.delay.priority = 80;
    char trackee[] = "trackee";

    do_track(&m_player, trackee, nullptr, CMD_TRACK, 0);

    EXPECT_TRUE(output_has("Possible bug - double delay.")) << output();
    ASSERT_NE(nullptr, txt_block_pool);
    EXPECT_STREQ("trackee", txt_block_pool->text);
    EXPECT_EQ(80, m_player.delay.priority) << "the delay running is kept";
    m_player.delay.wait_value = 0;
    m_player.delay.priority = 0;
}
