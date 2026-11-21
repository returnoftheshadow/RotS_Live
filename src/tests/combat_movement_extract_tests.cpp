/* Moving, fleeing and fighting while a script, set off on the way, removes one of the characters
 * involved (EXTRACT_CHAR): whoever was moving, fleeing or fighting stops touching the removed one.
 *
 * The scripts here belong to mobs waiting in the second room or to the one being hit. A test
 * that needs a roll to go one way queues it with push_test_random_value. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../skill_timer.h"
#include "../spells.h"

ACMD(do_move);
ACMD(do_flee);
ACMD(do_hit);
ACMD(do_kick);
ACMD(do_say);
ACMD(do_rend);
ACMD(do_bite);
ACMD(do_maul);
ACMD(do_cleave);
ACMD(do_stomp);
ACMD(do_smash);
ACMD(do_bash);
ACMD(do_rescue);
ACMD(do_disengage);
ACMD(do_track);
void perform_violence();
void on_windblast_success(char_data* character, int mana_cost, int move_cost);
void on_windblast_hit(char_data* character);
void die(char_data* dead_man, char_data* killer, int attack_type);
void affect_update_person(struct char_data* character, int mode);
void assign_command_pointers(void);
SPECIAL(thuringwethil);

extern struct index_data* mob_index;
extern int top_of_mobt;
extern struct txt_block* txt_block_pool;

using namespace scripted_room;

namespace {

/* Enough movement points for any step. */
constexpr int PLENTY_OF_MOVES = 100;
/* An offensive bonus that lands a blow whatever the rolls. */
constexpr int SURE_TO_LAND_OB = 200;
/* The damage bonus of a blow that hurts. */
constexpr int STRIKER_DAMAGE = 10;

/* A mob procedure that removes whoever arrives in its room. Written out rather than through
 * SPECIAL, so that it names only the parameters it reads. */
int remove_whoever_arrives(char_data*, char_data* actor, int, char*, int callflag, waiting_type*)
{
    if (callflag == SPECIAL_ENTER) {
        extract_char(actor);
    }
    return FALSE;
}

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

    /* A mob in room 0 sure to land a blow that hurts: its offensive bonus beats any roll, and
     * only a blow with a damage bonus does harm without a weapon. */
    char_data* make_sure_striker(const char* keywords)
    {
        char_data* striker = make_walker(keywords);
        striker->points.OB = SURE_TO_LAND_OB;
        striker->points.damage = STRIKER_DAMAGE;
        striker->specials.ENERGY = ENE_TO_HIT * 2;
        return striker;
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

// A roll that number() turns into its lowest result.
constexpr double LOWEST_ROLL = 0.0;

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

/* A guard whose script returns false keeps the walker out. */
TEST_F(CombatMovementTest, AWalkerTheRoomRefusesStaysWhereItIs)
{
    install_script(ON_BEFORE_ENTER, { { SCRIPT_RETURN_FALSE, nullptr, {} } });
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* walker = make_walker("walker");

    walk_north(walker);

    EXPECT_EQ(0, walker->in_room);
}

/* A follower removed before it enters: the follow goes no further than the check of its leader.
 * AddressSanitizer is what protects this test: only a follow reads the mover after that check. */
TEST_F(CombatMovementTest, AFollowerRemovedBeforeItEntersIsLetGo)
{
    install_guard_and_watcher(ON_BEFORE_ENTER);
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* leader = make_walker("leader");
    char_data* follower = make_walker("follower");
    add_follower(follower, leader, FOLLOW_MOVE);

    do_move(follower, empty_argument(), nullptr, NORTH + 1, SCMD_FOLLOW);

    EXPECT_FALSE(is_in_game(follower));
    EXPECT_EQ(0, leader->in_room);
}

/* The same for a follower on a mount. AddressSanitizer is what protects this test too. */
TEST_F(CombatMovementTest, AMountedFollowerRemovedBeforeItEntersIsLetGo)
{
    install_guard_and_watcher(ON_BEFORE_ENTER);
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* leader = make_walker("leader");
    char_data* mount = make_walker("horse");
    char_data* follower = make_walker("follower");
    seat(follower, mount);
    add_follower(follower, leader, FOLLOW_MOVE);

    do_move(follower, empty_argument(), nullptr, NORTH + 1, SCMD_FOLLOW);

    EXPECT_FALSE(is_in_game(follower));
    EXPECT_EQ(0, mount->in_room);
}

/* The guard lets the rider through, and when it checks the mount has it say a word that makes the
 * rider, hearing it, remove itself: the ride ends there. AddressSanitizer is what protects this
 * test: the ride would go on to charge the rider for it. */
TEST_F(CombatMovementTest, ARiderRemovedAsItsMountIsCheckedIsLetGo)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_BEFORE_ENTER,
                              { { SCRIPT_IF_STR_EQUAL, "horse", { SCRIPT_PARAM_CH2_NAME } },
                                  { SCRIPT_DO_SAY, "vanish", { SCRIPT_PARAM_CH2 } } } } } },
        { OTHER_SCRIPT_VNUM, { { ON_HEAR_SAY, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } } } } } });
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* mount = make_walker("horse");
    char_data* rider = make_walker("rider");
    rider->specials.script_number = OTHER_SCRIPT_VNUM;
    seat(rider, mount);

    walk_north(rider);

    EXPECT_FALSE(is_in_game(rider));
    EXPECT_EQ(0, mount->in_room);
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

/* A procedure where they arrive removes the rider, and then the mount: no script is set off for
 * either. AddressSanitizer is what protects this test: nothing else reads the rider afterwards. */
TEST_F(CombatMovementTest, ARiderAProcedureRemovesWhereItArrivesSetsOffNoScript)
{
    char_data* host = make_mob("host", 0, SECOND_ROOM);
    give_prototype(host);
    SET_BIT(MOB_FLAGS(host), MOB_SPEC);
    m_prototype_index[0].func = remove_whoever_arrives;
    const int saved_top_of_mobt = top_of_mobt;
    top_of_mobt = 0;
    char_data* mount = make_walker("horse");
    char_data* rider = make_walker("rider");
    seat(rider, mount);

    walk_north(rider);

    top_of_mobt = saved_top_of_mobt;
    EXPECT_FALSE(is_in_game(rider));
    EXPECT_FALSE(is_in_game(mount));
}

/* Script SCRIPT_VNUM is a guard that says a word when a mob named rider arrives, and script
 * OTHER_SCRIPT_VNUM removes its own mob when it hears anything. */
static std::vector<script_spec> speak_as_the_rider_arrives_scripts()
{
    return { { SCRIPT_VNUM,
                 { { ON_ENTER,
                     { { SCRIPT_IF_STR_EQUAL, "rider", { SCRIPT_PARAM_CH2_NAME } },
                         { SCRIPT_DO_SAY, "vanish", { SCRIPT_PARAM_CH1 } } } } } },
        { OTHER_SCRIPT_VNUM, { { ON_HEAR_SAY, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } } } } } };
}

/* The passenger hears the guard greet the rider, ahead of it, and removes itself: the mount and
 * the rider arrive all the same. */
TEST_F(CombatMovementTest, APassengerRemovedAsTheRiderAheadArrivesIsLetGo)
{
    install_scripts(speak_as_the_rider_arrives_scripts());
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* mount = make_walker("horse");
    char_data* rider = make_walker("rider");
    char_data* passenger = make_walker("passenger");
    passenger->specials.script_number = OTHER_SCRIPT_VNUM;
    seat(rider, mount);
    seat(passenger, mount);

    walk_north(rider);

    EXPECT_FALSE(is_in_game(passenger));
    EXPECT_EQ(SECOND_ROOM, mount->in_room);
    EXPECT_EQ(SECOND_ROOM, rider->in_room);
}

/* The mount hears the guard greet its rider, and removes itself: the move has happened, and the
 * rider stays where it arrived. AddressSanitizer is what protects this test: the mount would be
 * offered its own arrival next. */
TEST_F(CombatMovementTest, AMountRemovedAsItsRiderArrivesIsLetGo)
{
    install_scripts(speak_as_the_rider_arrives_scripts());
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* mount = make_walker("horse");
    mount->specials.script_number = OTHER_SCRIPT_VNUM;
    char_data* rider = make_walker("rider");
    seat(rider, mount);

    walk_north(rider);

    EXPECT_FALSE(is_in_game(mount));
    EXPECT_EQ(SECOND_ROOM, rider->in_room);
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

// A roll that beats a test mob's save against the wind: it is pushed out.
constexpr double PUSHING_ROLL = 0.99;
// A dexterity whose save against the wind the lowest roll does not beat.
constexpr int WIND_RESISTING_DEXTERITY = 120;

// The wind pushes the victim out, where it is removed: the caster does not go on to hurt it.
TEST_F(CombatMovementTest, AWindblastVictimRemovedWhereItLandsIsNotHurt)
{
    install_guard_and_watcher(ON_BEFORE_ENTER);
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* caster = make_walker("caster");
    char_data* first_victim = make_walker("first");
    char_data* second_victim = make_walker("second");
    second_victim->tmpabilities.dex = WIND_RESISTING_DEXTERITY; // it saves, and is only hurt
    // The player stands aside: pushing a player out would move its race's power between zones.
    world[0].people = m_player.next_in_room;
    push_test_random_value(LOWEST_ROLL); // the damage roll
    push_test_random_value(PUSHING_ROLL); // the first victim's save: it fails and is pushed
    push_test_random_value(LOWEST_ROLL); // the direction it is pushed
    push_test_random_value(LOWEST_ROLL); // the second victim's save

    on_windblast_success(caster, 0, 0);

    EXPECT_TRUE(is_in_game(caster));
    EXPECT_FALSE(is_in_game(first_victim));
    EXPECT_TRUE(is_in_game(second_victim)) << "the blast went on to the next one";
}

// Too tired to strike, the attacker still frightens its badly hurt victim into fleeing; the
// victim is removed where it flees to, and the attack lets it go.
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

/* The wind pushes its victim out, where it is removed. AddressSanitizer is what protects this
 * test: the wind would go on to try the freed victim's other exits. */
TEST_F(CombatMovementTest, AVictimTheWindPushesIsLetGoWhereAGuardRemovesIt)
{
    install_guard_and_watcher(ON_BEFORE_ENTER);
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* victim = make_walker("victim");

    on_windblast_hit(victim);

    EXPECT_FALSE(is_in_game(victim));
}

/* The asker answers "go" by saying "x1" and fleeing. Two mobs, where there are any, answer each
 * "x" with two says, without end. The guard's ON_BEFORE_ENTER script tells the room it checked. */
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

/* With no answerers the guard's script runs and lets the asker through. */
TEST_F(CombatMovementTest, AGuardWhoseScriptRunsLetsTheFleerIn)
{
    install_scripts(exhaust_then_flee_scripts());
    char_data* speaker = make_walker("speaker");
    char_data* asker = make_walker("asker");
    asker->specials.script_number = SCRIPT_VNUM;
    make_mob("guard", THIRD_SCRIPT_VNUM, SECOND_ROOM);
    world[0].people = m_player.next_in_room;

    run_command_as(speaker, do_say, "go");

    EXPECT_EQ(SECOND_ROOM, asker->in_room);
}

// What the ping mob adds to its run count, int1, on each run.
constexpr int ONE_RUN = 1;

// Scripts for a chain of runs inside one another as deep as runs may go. Script SCRIPT_VNUM answers
// whatever it hears with "pong", and tells the room "Ouch." when it is hurt. Script
// OTHER_SCRIPT_VNUM counts its runs in int1 and answers with "ping", but on run
// <acting_run> does <action> instead. Script THIRD_SCRIPT_VNUM is a guard that checks whoever
// enters its room.
//
// The pong mob, made first, hears the player first, so the ping mob's run n is at depth 2n: its
// run 128 is at MAX_SCRIPT_NESTING, where no further run may start, and its run 127 two below.
static std::vector<script_spec> deepest_run_scripts(int acting_run, const body_line& action)
{
    return { { SCRIPT_VNUM,
                 { { ON_HEAR_SAY, { { SCRIPT_DO_SAY, "pong", { SCRIPT_PARAM_CH1 } } } },
                     { ON_DAMAGE, { { SCRIPT_SEND_TO_ROOM, "Ouch.", { SCRIPT_PARAM_CH1_ROOM } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_HEAR_SAY,
                { { SCRIPT_SET_INT_VALUE, nullptr, { ONE_RUN, SCRIPT_PARAM_INT2 } },
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

// The run the ping mob makes at the deepest a run may go.
constexpr int DEEPEST_PING_RUN = MAX_SCRIPT_NESTING / 2;

// The ping mob flees at the deepest run: the guard's script cannot run, so the guard keeps it out
// (a refused ON_BEFORE_ENTER fails closed).
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
    const std::string refusal
        = "script #" + std::to_string(THIRD_SCRIPT_VNUM) + " not run: too many scripts are running";
    EXPECT_NE(std::string::npos, logged.find(refusal)) << logged;
}

// Two runs shallower the guard's script runs and lets the pinger in.
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

// The ping mob hits the pong mob at the deepest run: the pong mob's damage script cannot run, so
// the blow is not dealt (a refused ON_DAMAGE fails closed).
TEST_F(CombatMovementTest, ABlowWhoseDamageScriptIsTooDeepIsNotDealt)
{
    install_scripts(deepest_run_scripts(
        DEEPEST_PING_RUN, { SCRIPT_DO_HIT, nullptr, { SCRIPT_PARAM_CH1, SCRIPT_PARAM_CH2 } }));
    char_data* ponger = make_walker("ponger");
    ponger->specials.script_number = SCRIPT_VNUM;
    char_data* pinger = make_walker("pinger");
    pinger->specials.script_number = OTHER_SCRIPT_VNUM;
    pinger->points.OB = SURE_TO_LAND_OB;
    pinger->specials.ENERGY = ENE_TO_HIT * 2; // ready to strike
    equip_char(pinger, make_object(SWORD_VNUM, 0), WIELD); // a blow that hurts

    testing::internal::CaptureStderr();
    run_command(do_say, "go");
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(0, count_in_output("Ouch."));
    EXPECT_EQ(ponger->abilities.hit, ponger->tmpabilities.hit);
}

// Two runs shallower the damage script runs, and the blow is dealt.
TEST_F(CombatMovementTest, ABlowWhoseDamageScriptIsJustDeepEnoughIsDealt)
{
    install_scripts(deepest_run_scripts(
        DEEPEST_PING_RUN - 1, { SCRIPT_DO_HIT, nullptr, { SCRIPT_PARAM_CH1, SCRIPT_PARAM_CH2 } }));
    char_data* ponger = make_walker("ponger");
    ponger->specials.script_number = SCRIPT_VNUM;
    char_data* pinger = make_walker("pinger");
    pinger->specials.script_number = OTHER_SCRIPT_VNUM;
    pinger->points.OB = SURE_TO_LAND_OB;
    pinger->specials.ENERGY = ENE_TO_HIT * 2; // ready to strike
    equip_char(pinger, make_object(SWORD_VNUM, 0), WIELD); // a blow that hurts

    testing::internal::CaptureStderr();
    run_command(do_say, "go");
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(1, count_in_output("Ouch."));
    EXPECT_LT(ponger->tmpabilities.hit, ponger->abilities.hit);
}

// ---- skills and special procedures ----

// The id the first olog-hai of these tests gets, past any id the other tests give.
constexpr long FIRST_OLOG_HAI_IDNUM = 900000;
// How well the olog-hai knows its skills: as well as a mob knows every skill.
constexpr int OLOG_HAI_SKILL_LEVEL = 80;

// Makes the player an olog-hai who knows its skills and wields a sword (a mob olog-hai cannot use
// them: its damage divides by its tactics, which a mob does not have).
static void make_olog_hai(char_data& player, obj_data* sword)
{
    // The skill timers outlive a test and are kept by id: each test's olog-hai is someone new.
    static long next_idnum = FIRST_OLOG_HAI_IDNUM;
    player.specials2.idnum = next_idnum++;
    player.player.race = RACE_OLOGHAI;
    for (int skill : { SKILL_SMASH, SKILL_STOMP, SKILL_CLEAVE }) {
        player.knowledge[skill] = OLOG_HAI_SKILL_LEVEL;
        player.skills[skill] = OLOG_HAI_SKILL_LEVEL;
    }
    equip_char(&player, sword, WIELD);
    player.points.OB = SURE_TO_LAND_OB;
}

// The body of a damage script that removes the attacker (ch2).
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

    SET_BIT(victim->specials.affected_by, AFF_BASH); // only a bashed victim is rent

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

// A roll high enough that an olog-hai's skill lands, and that a smash knocks a rider off.
constexpr double LANDING_ROLL = 0.99;
// How long a paused victim's script waits: only a delay that ends the pause early resumes it.
constexpr int PAUSE_PULSES = 5;
// The hit points any blow takes: a smash fells the rider.
constexpr int LAST_HIT_POINT = 1;

// The olog-hai's cleave hits everyone in the room; each victim's damage script removes it, and the
// cleave still goes on to the next.
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

// Stomp knocks its victim down before hurting it; the victim was paused in a script whose rest,
// run at once by the knock-down, removes it: the stomp does not hurt what is gone.
TEST_F(CombatMovementTest, AStompOnAPausedVictimItsScriptRemovesHurtsNobody)
{
    install_script(ON_HEAR_SAY,
        { { SCRIPT_DO_WAIT, nullptr, { PAUSE_PULSES } },
            { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    make_olog_hai(m_player, make_object(SWORD_VNUM, 0));
    char_data* victim = make_walker("victim");
    victim->specials.script_number = SCRIPT_VNUM;
    run_command(do_say, "hello");
    ASSERT_EQ(CMD_SCRIPT, victim->delay.cmd);
    push_test_random_value(LANDING_ROLL); // the stomp's to-hit roll

    do_stomp(&m_player, empty_argument(), nullptr, 0, 0);

    EXPECT_FALSE(is_in_game(victim)) << output();
}

// A smash fells a mounted victim whose death script refuses death: the victim is still there, so
// it is knocked off its mount as any smash of a rider does.
TEST_F(CombatMovementTest, ASmashOnARiderWhoRefusesToDieStillUnseatsIt)
{
    install_script(ON_DIE, { { SCRIPT_RETURN_FALSE, nullptr, {} } });
    make_olog_hai(m_player, make_object(SWORD_VNUM, 0));
    char_data* rider = make_walker("rider");
    rider->specials.script_number = SCRIPT_VNUM;
    rider->tmpabilities.hit = LAST_HIT_POINT;
    char_data* horse = make_walker("horse");
    seat(rider, horse);
    waiting_type target {};
    target.targ1.set_character(rider);
    push_test_random_value(LANDING_ROLL); // the to-hit roll
    push_test_random_value(LANDING_ROLL); // the smash's roll to knock a rider off

    do_smash(&m_player, empty_argument(), &target, 0, 0);

    ASSERT_TRUE(is_in_game(rider));
    EXPECT_FALSE(IS_RIDING(rider)) << output();
}

// Thuringwethil at this many hit points or fewer is beaten and leaves the game (her procedure).
constexpr int THURINGWETHIL_BEATEN_HIT_POINTS = 500;
// A blow too light to kill anyone with her hit points.
constexpr int LIGHT_BLOW_DAMAGE = 10;

// Thuringwethil's procedure has her leave once she is down to 500 hit points, whatever event
// calls it: a blow at her through damage() lets go of her when she does.
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
    char_prof_data* vampire_profs = vampire->profs; // free_char leaves it to the prototype
    SET_BIT(MOB_FLAGS(vampire), MOB_SPEC);
    vampire->tmpabilities.hit = THURINGWETHIL_BEATEN_HIT_POINTS;

    EXPECT_EQ(1, damage(attacker, vampire, LIGHT_BLOW_DAMAGE, TYPE_HIT, 0));

    EXPECT_FALSE(is_in_game(vampire));
    EXPECT_TRUE(is_in_game(attacker));
    mob_index = saved_mob_index;
    top_of_mobt = saved_top_of_mobt;
    RELEASE(vampire_profs);
}

// ---- delays that run the rest of a paused script ----

// do_bash's stage that knocks down the character it waited on (its subcommand).
constexpr int BASH_CHARACTER_STAGE = 1;

// A bash knocks its victim down, which ends the victim's pause: the rest of its script makes the
// basher vanish, and the basher then does not go on to hurt anyone.
TEST_F(CombatMovementTest, ABasherTheBashedMobsPausedScriptRemovesStopsThere)
{
    install_pause_then_vanish_scripts();
    char_data* victim = make_walker("victim");
    victim->specials.script_number = SCRIPT_VNUM;
    char_data* basher = make_walker("basher");
    basher->specials.script_number = OTHER_SCRIPT_VNUM;
    basher->points.OB = SURE_TO_LAND_OB; // a mob knows every skill at 80
    run_command_as(basher, do_say, "hello");
    ASSERT_EQ(CMD_SCRIPT, victim->delay.cmd);
    waiting_type target {};
    target.targ1.set_character(victim);

    do_bash(basher, empty_argument(), &target, CMD_BASH, BASH_CHARACTER_STAGE);

    EXPECT_FALSE(is_in_game(basher));
    EXPECT_TRUE(is_in_game(victim));
    EXPECT_EQ(victim->tmpabilities.hit, victim->abilities.hit) << "nobody was left to hurt it";
}

// A rescue holds up the one rescued, which ends its pause: the rest of its script makes the
// rescuer vanish, and the rescuer is then not held up in turn.
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
    push_test_random_value(LOWEST_ROLL); // the rescue succeeds (a mob knows it at 80)
    char rescued_name[] = "rescued";

    do_rescue(rescuer, rescued_name, &target, CMD_RESCUE, 0);

    EXPECT_FALSE(is_in_game(rescuer)) << output();
    EXPECT_TRUE(is_in_game(rescued));
}

// Disengaging ends the disengager's pause: the rest of its script makes the one it fights vanish,
// which ends the fight, and there is nobody left to stop fighting.
TEST_F(CombatMovementTest, ADisengageWhosePausedScriptEndsTheFightStopsThere)
{
    install_pause_then_vanish_scripts();
    char_data* disengager = make_walker("disengager");
    disengager->specials.script_number = SCRIPT_VNUM;
    char_data* foe = make_walker("foe");
    foe->specials.script_number = OTHER_SCRIPT_VNUM;
    run_command(do_say, "hello");
    ASSERT_EQ(CMD_SCRIPT, disengager->delay.cmd);
    set_fighting(disengager, foe); // the foe does not fight back, so it lets the disengager go

    do_disengage(disengager, empty_argument(), nullptr, 0, 0);

    EXPECT_FALSE(is_in_game(foe));
    ASSERT_TRUE(is_in_game(disengager));
    EXPECT_EQ(nullptr, disengager->specials.fighting);
    EXPECT_EQ(0, count_in_output("stops fighting"));
}

// How long the fear the test gives lasts, in hours: it is still on when its tick comes.
constexpr int FEAR_HOURS = 5;
// affect_update_person's mode for the hourly pass, as against fast_update's.
constexpr int HOURLY_AFFECT_UPDATE = 0;

// Fear makes a mob flee into a room whose guard removes it: the fear does not go on with it.
TEST_F(CombatMovementTest, AMobFearDrivesIntoItsRemovalIsLetGo)
{
    install_scripts({ { SCRIPT_VNUM,
        { { ON_BEFORE_ENTER, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } } } } } });
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* frightened = make_walker("frightened");
    frightened->specials.position = POSITION_FIGHTING;
    affected_type fear {};
    fear.type = SPELL_FEAR;
    fear.duration = FEAR_HOURS;
    fear.location = APPLY_NONE;
    affect_to_char(frightened, &fear);

    affect_update_person(frightened, HOURLY_AFFECT_UPDATE);

    EXPECT_FALSE(is_in_game(frightened));
}

/* ---- blows ---- */

/* The victim's damage script removes whoever strikes it. The first fighter's blow removes it, and
 * the round goes on: the second fighter's blow sets the same script off. */
TEST_F(CombatMovementTest, TheCombatRoundLetsGoOfAFighterItsOwnBlowRemoved)
{
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    char_data* first_fighter = make_sure_striker("first");
    char_data* second_fighter = make_sure_striker("second");
    char_data* victim = make_walker("victim");
    victim->specials.script_number = SCRIPT_VNUM;
    first_fighter->specials.position = POSITION_FIGHTING;
    second_fighter->specials.position = POSITION_FIGHTING;
    set_fighting(first_fighter, victim);
    set_fighting(second_fighter, victim);

    perform_violence();

    EXPECT_FALSE(is_in_game(first_fighter));
    EXPECT_FALSE(is_in_game(second_fighter));
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

// ---- scripts refused past the limits ----

// With two mobs answering the "x", the event's scripts are used up before the asker flees: the
// guard's script, refused, keeps the asker out.
TEST_F(CombatMovementTest, AGuardWhoseScriptIsRefusedKeepsTheDoorShut)
{
    install_scripts(exhaust_then_flee_scripts());
    char_data* speaker = make_walker("speaker");
    char_data* asker = make_walker("asker");
    asker->specials.script_number = SCRIPT_VNUM;
    make_mob("first answerer", OTHER_SCRIPT_VNUM);
    make_mob("second answerer", OTHER_SCRIPT_VNUM);
    make_mob("guard", THIRD_SCRIPT_VNUM, SECOND_ROOM);
    world[0].people = m_player.next_in_room; // the player stands aside

    testing::internal::CaptureStderr();
    run_command_as(speaker, do_say, "go");
    testing::internal::GetCapturedStderr();

    EXPECT_EQ(0, asker->in_room) << "refused, the guard keeps the asker out";
}

/* The hit command's blow removes the attacker. AddressSanitizer is what protects this test: the
 * command would go on to make the attacker wait. */
TEST_F(CombatMovementTest, AnAttackerItsOwnBlowRemovedIsNotMadeToWait)
{
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    char_data* attacker = make_sure_striker("attacker");
    char_data* victim = make_walker("victim");
    victim->specials.script_number = SCRIPT_VNUM;

    run_command_as(attacker, do_hit, "victim");

    EXPECT_FALSE(is_in_game(attacker));
    EXPECT_TRUE(is_in_game(victim));
}

/* What a blow deals that leaves its badly hurt victim alive to flee. */
constexpr int NO_DAMAGE = 0;

/* The blow frightens its badly hurt victim into fleeing, and the victim is removed where it flees
 * to: the blow says the victim is gone. */
TEST_F(CombatMovementTest, ABlowLetsGoOfAWimpyVictimRemovedWhereItFlees)
{
    install_guard_and_watcher(ON_BEFORE_ENTER);
    make_mob("guard", SCRIPT_VNUM, SECOND_ROOM);
    char_data* attacker = make_walker("attacker");
    char_data* victim = make_walker("victim");
    make_ready_to_flee(victim);

    EXPECT_EQ(1, damage(attacker, victim, NO_DAMAGE, TYPE_HIT, 0));

    EXPECT_FALSE(is_in_game(victim));
    EXPECT_TRUE(is_in_game(attacker));
}

/* ---- dying ---- */

/* Experience a victim is worth gaining from. */
constexpr int EXPERIENCE_WORTH_GAINING = 5000;

/* A kill pays the killer: what the tests of a mob removed as it dies, which pays nothing, are set
 * against. */
TEST_F(CombatMovementTest, AKillPaysTheKiller)
{
    char_data* dying = make_walker("dying");
    dying->points.exp = EXPERIENCE_WORTH_GAINING;
    give_prototype(dying);
    const int experience_before = GET_EXP(&m_player);

    die(dying, &m_player, 0);

    EXPECT_FALSE(is_in_game(dying));
    EXPECT_GT(GET_EXP(&m_player), experience_before);
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

// A mob slain by a mob keeps the wearables in its containers where they are, even when its death
// script removes the killer before the corpse is made.
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

// A death with no killer at all takes the wearables out of the dead one's containers into the
// corpse.
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

/* The delay a bash leaves its victim (as olog_hai's bash and the trap give it): its priority
 * outranks tracking's, 30, and any pulses left keep it running. */
constexpr int BASHED_DELAY_PRIORITY = 80;
constexpr int BASHED_DELAY_PULSES = 5;

/* Tracking while a delay that outranks it runs is a double delay: tracking does not start, and the
 * text block it took for the name to track goes back to the pool. */
TEST_F(CombatMovementTest, ATrackThatCannotStartGivesBackItsTextBlock)
{
    m_player.delay.wait_value = BASHED_DELAY_PULSES;
    m_player.delay.priority = BASHED_DELAY_PRIORITY;
    char trackee[] = "trackee";

    do_track(&m_player, trackee, nullptr, CMD_TRACK, 0);

    EXPECT_TRUE(output_has("Possible bug - double delay.")) << output();
    ASSERT_NE(nullptr, txt_block_pool);
    EXPECT_STREQ("trackee", txt_block_pool->text);
    EXPECT_EQ(BASHED_DELAY_PRIORITY, m_player.delay.priority) << "the delay running is kept";
    m_player.delay.wait_value = 0;
    m_player.delay.priority = 0;
}
