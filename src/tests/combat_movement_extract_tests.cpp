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
