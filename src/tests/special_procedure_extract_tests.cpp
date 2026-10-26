/* Special procedures that remove characters or objects while a command, a move, a mob's turn or a
 * dragon's breath is still going through them: whatever offered the procedure stops touching
 * what was removed, and goes on as before with what is left.
 *
 * The procedures here are test procedures given to mobs (through a prototype entry), objects
 * (through their prototype's index entry) and rooms; procedure_record tells what they did. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../skill_timer.h"

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

void do_move(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_say(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void raw_kill(char_data* dead_man, const char_data* killer, int attack_type);
void one_mobile_activity(char_data* ch);
void mobile_activity(void);
void assign_command_pointers(void);
SPECIAL(dragon);
SPECIAL(gatekeeper);
SPECIAL(gatekeeper2);
SPECIAL(thuringwethil);

extern struct index_data* mob_index;
extern int top_of_mobt;
extern struct weather_data weather_info;

using namespace scripted_room;

namespace {

/* Enough movement points for any step. */
constexpr int PLENTY_OF_MOVES = 100;
/* How many mobs one test can give a prototype entry (and with it a procedure). */
constexpr int MOB_PROTOTYPE_SLOTS = 8;

/* What the test procedures act on and what they saw, cleared before each test. */
struct procedure_record {
    // The character remove_marked_character and remove_marked_on_death remove.
    char_data* character_to_remove = nullptr;
    // The object remove_marked_object removes.
    obj_data* object_to_remove = nullptr;
    // The hosts record_offer ran for, in order.
    std::vector<const void*> offered_hosts;
};

procedure_record record;

/* Notes that <host> was offered the command. It reads the one acting, as most procedures do, so
 * a freed one handed to it is a use after free that AddressSanitizer reports. */
SPECIAL(record_offer)
{
    if (ch && ch->in_room == NOWHERE) {
        ADD_FAILURE() << "the one acting is nowhere";
    }
    record.offered_hosts.push_back(host);
    return 0;
}

/* Takes the command for itself: no procedure after it is offered it. */
SPECIAL(intercept_command)
{
    record.offered_hosts.push_back(host);
    return 1;
}

/* Removes its own mob, and lets the command go on. */
SPECIAL(remove_own_mob)
{
    extract_char(host);
    return 0;
}

/* Removes its own object, and lets the command go on. */
SPECIAL(remove_own_object)
{
    extract_obj(reinterpret_cast<obj_data*>(host));
    return 0;
}

/* Removes the one acting, and lets the command go on. */
SPECIAL(remove_actor)
{
    extract_char(ch);
    return 0;
}

/* A guard: removes whoever sets off north past it, and lets the command go on. */
SPECIAL(remove_actor_going_north)
{
    if (callflag == SPECIAL_COMMAND && cmd == NORTH + 1) {
        extract_char(ch);
    }
    return 0;
}

/* Removes record.character_to_remove when anyone acts, and lets the command go on. */
SPECIAL(remove_marked_character)
{
    if (record.character_to_remove) {
        char_data* marked = record.character_to_remove;
        record.character_to_remove = nullptr;
        extract_char(marked);
    }
    return 0;
}

/* As remove_marked_character, but only when someone sets off north. */
SPECIAL(remove_marked_going_north)
{
    if (callflag == SPECIAL_COMMAND && cmd == NORTH + 1) {
        return remove_marked_character(host, ch, cmd, arg, callflag, wtl);
    }
    return 0;
}

/* As remove_marked_character, but only for the one arriving being record.character_to_remove. */
SPECIAL(remove_marked_arriving)
{
    if (callflag == SPECIAL_ENTER && ch == record.character_to_remove) {
        return remove_marked_character(host, ch, cmd, arg, callflag, wtl);
    }
    return 0;
}

/* As remove_marked_character, but only when its own mob dies. */
SPECIAL(remove_marked_on_death)
{
    if (callflag == SPECIAL_DEATH) {
        return remove_marked_character(host, ch, cmd, arg, callflag, wtl);
    }
    return 0;
}

/* Removes record.object_to_remove, and lets the command go on. */
SPECIAL(remove_marked_object)
{
    if (record.object_to_remove) {
        obj_data* marked = record.object_to_remove;
        record.object_to_remove = nullptr;
        extract_obj(marked);
    }
    return 0;
}

/* On its own mob's turn, removes record.character_to_remove and then the mob itself. */
SPECIAL(remove_marked_then_self)
{
    if (callflag == SPECIAL_SELF) {
        remove_marked_character(host, ch, cmd, arg, callflag, wtl);
        extract_char(host);
    }
    return 0;
}

class SpecialProcedureTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        link_second_room();
        /* Made at boot; damage and deaths ask them. Making them again keeps the ones there are. */
        game_rules::big_brother::create(weather_info, &world);
        game_timer::skill_timer::create(weather_info, &world);
        record = procedure_record {};
        m_saved_mob_index = mob_index;
        m_saved_top_of_mobt = top_of_mobt;
        mob_index = m_mob_prototypes;
        top_of_mobt = MOB_PROTOTYPE_SLOTS - 1;
        for (int room = 0; room <= SECOND_ROOM; ++room) {
            m_saved_room_procedures[room] = world[room].funct;
            m_saved_room_flags[room] = world[room].room_flags;
            world[room].funct = nullptr;
            world[room].room_flags = 0;
        }
    }

    void TearDown() override
    {
        /* The mobs still in the game are extracted with their prototype entries in place. */
        ScriptedRoomFixture::TearDown();
        for (char_prof_data*& profs : m_mob_profs)
            RELEASE(profs);
        mob_index = m_saved_mob_index;
        top_of_mobt = m_saved_top_of_mobt;
        for (int room = 0; room <= SECOND_ROOM; ++room) {
            world[room].funct = m_saved_room_procedures[room];
            world[room].room_flags = m_saved_room_flags[room];
        }
        record = procedure_record {};
    }

    /* Gives <mob> a prototype entry of its own running <procedure> (none for null), as a mob read
     * from the world files has. The game then no longer frees the mob's strings or profession
     * block, so the test keeps them. */
    void give_prototype(char_data* mob, special_func procedure)
    {
        if (m_used_prototypes >= MOB_PROTOTYPE_SLOTS) {
            ADD_FAILURE() << "more than " << MOB_PROTOTYPE_SLOTS << " mobs with prototypes";
            return;
        }
        const int slot = m_used_prototypes++;
        m_mob_prototypes[slot].func = procedure;
        mob->nr = slot;
        if (procedure) {
            SET_BIT(MOB_FLAGS(mob), MOB_SPEC);
        }
        m_mob_names.emplace_back(mob->player.name);
        RELEASE(mob->player.name);
        RELEASE(mob->player.short_descr);
        mob->player.name = m_mob_names.back().data();
        mob->player.short_descr = m_mob_names.back().data();
        m_mob_profs.push_back(mob->profs);
    }

    /* A mob in <room> with a prototype entry running <procedure>, and the moves to walk anywhere. */
    char_data* make_mob_with(const char* keywords, special_func procedure, int room = 0)
    {
        char_data* mob = make_mob(keywords, 0, room);
        mob->abilities.move = PLENTY_OF_MOVES;
        mob->tmpabilities.move = PLENTY_OF_MOVES;
        give_prototype(mob, procedure);
        return mob;
    }

    /* An object of the prototype with <vnum>, whose prototype runs <procedure>; not placed. */
    obj_data* make_object_with(int vnum, special_func procedure)
    {
        obj_data* object = make_object(vnum, 0);
        obj_index[object->item_number].func = procedure;
        return object;
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

    /* Moves <walker> north, as the interpreter does for the north command. */
    static void walk_north(char_data* walker) { do_move(walker, empty_argument(), nullptr, NORTH + 1, 0); }

    /* Offers <ch>'s command <cmd> to the procedures here, as the interpreter does. */
    static int offer_command(char_data* ch, int cmd = NORTH + 1)
    {
        return special(ch, cmd, empty_argument(), SPECIAL_COMMAND, nullptr);
    }

    /* A writable empty argument for the commands. */
    static char* empty_argument()
    {
        static char empty[1] = "";
        empty[0] = '\0';
        return empty;
    }

    /* Whether record_offer or intercept_command ran for <host>. */
    static bool was_offered(const void* host)
    {
        const std::vector<const void*>& offered = record.offered_hosts;
        return std::find(offered.begin(), offered.end(), host) != offered.end();
    }

    /* Puts <first> and <second> in a fight with each other. */
    static void start_fight(char_data* first, char_data* second)
    {
        set_fighting(first, second);
        set_fighting(second, first);
    }

private:
    // The prototype entries give_prototype hands out, in place of the game's mob_index.
    index_data m_mob_prototypes[MOB_PROTOTYPE_SLOTS] {};
    // How many of m_mob_prototypes give_prototype has handed out.
    int m_used_prototypes = 0;
    // The names of the mobs with prototype entries, which the game no longer frees; a deque, so
    // earlier names never move.
    std::deque<std::string> m_mob_names;
    // The profession blocks clear_char gave the mobs with prototype entries, which free_char then
    // leaves alone as it leaves a prototype's; released by TearDown.
    std::vector<char_prof_data*> m_mob_profs;
    // The game's mob prototype entries and their count, put back by TearDown.
    index_data* m_saved_mob_index = nullptr;
    int m_saved_top_of_mobt = 0;
    // The procedures and flags of rooms 0 and SECOND_ROOM before the test, put back by TearDown.
    special_func m_saved_room_procedures[SECOND_ROOM + 1] {};
    long m_saved_room_flags[SECOND_ROOM + 1] {};
};

} // namespace

/* ---- special(): what a command is offered to ---- */

TEST_F(SpecialProcedureTest, EveryProcedureHereIsOfferedACommandNobodyTakes)
{
    char_data* actor = make_mob_with("actor", nullptr);
    world[0].funct = record_offer;
    obj_data* worn = make_object_with(RING_VNUM, record_offer);
    equip_char(actor, worn, WEAR_FINGER_R);
    obj_data* carried = make_object_with(STONE_VNUM, record_offer);
    obj_to_char(carried, actor);
    char_data* bystander = make_mob_with("bystander", record_offer);
    obj_data* lying = make_object_with(BREAD_VNUM, record_offer);
    obj_to_room(lying, 0);

    EXPECT_EQ(0, offer_command(actor));

    const std::vector<const void*> expected
        = { actor, worn, carried, bystander, lying };
    EXPECT_EQ(expected, record.offered_hosts);
}

TEST_F(SpecialProcedureTest, AProcedureThatTakesTheCommandIsTheLastOfferedIt)
{
    char_data* taker = make_mob_with("taker", intercept_command);
    char_data* bystander = make_mob_with("bystander", record_offer);

    EXPECT_EQ(1, offer_command(&m_player));

    EXPECT_TRUE(was_offered(taker));
    EXPECT_FALSE(was_offered(bystander));
}

TEST_F(SpecialProcedureTest, TheMobAfterOneThatRemovesItselfIsStillOfferedTheCommand)
{
    char_data* vanishing = make_mob_with("vanishing", remove_own_mob);
    char_data* bystander = make_mob_with("bystander", record_offer);

    EXPECT_EQ(0, offer_command(&m_player));

    EXPECT_FALSE(is_in_game(vanishing));
    EXPECT_TRUE(was_offered(bystander));
}

TEST_F(SpecialProcedureTest, TheItemAfterOneThatRemovesItselfIsStillOfferedTheCommand)
{
    obj_data* recording = make_object_with(RING_VNUM, record_offer);
    obj_to_char(recording, &m_player);
    obj_data* crumbling = make_object_with(STONE_VNUM, remove_own_object);
    obj_to_char(crumbling, &m_player);
    ASSERT_EQ(crumbling, m_player.carrying) << "the crumbling one is offered first";

    EXPECT_EQ(0, offer_command(&m_player));

    EXPECT_FALSE(is_in_object_list(crumbling));
    EXPECT_TRUE(was_offered(recording));
}

TEST_F(SpecialProcedureTest, TheObjectAfterOneThatRemovesItselfIsStillOfferedTheCommand)
{
    obj_data* recording = make_object_with(RING_VNUM, record_offer);
    obj_to_room(recording, 0);
    obj_data* crumbling = make_object_with(STONE_VNUM, remove_own_object);
    obj_to_room(crumbling, 0);
    ASSERT_EQ(crumbling, world[0].contents) << "the crumbling one is offered first";

    EXPECT_EQ(0, offer_command(&m_player));

    EXPECT_FALSE(is_in_object_list(crumbling));
    EXPECT_TRUE(was_offered(recording));
}

TEST_F(SpecialProcedureTest, ARoomThatRemovesTheOneActingEndsTheCommand)
{
    char_data* actor = make_mob_with("actor", nullptr);
    world[0].funct = remove_actor;
    char_data* bystander = make_mob_with("bystander", record_offer);

    EXPECT_EQ(1, offer_command(actor));

    EXPECT_FALSE(is_in_game(actor));
    EXPECT_FALSE(was_offered(bystander));
}

TEST_F(SpecialProcedureTest, AWornItemThatRemovesTheOneActingEndsTheCommand)
{
    char_data* actor = make_mob_with("actor", nullptr);
    equip_char(actor, make_object_with(RING_VNUM, remove_actor), WEAR_FINGER_R);
    equip_char(actor, make_object_with(SWORD_VNUM, record_offer), WIELD);
    char_data* bystander = make_mob_with("bystander", record_offer);

    EXPECT_EQ(1, offer_command(actor));

    EXPECT_FALSE(is_in_game(actor));
    EXPECT_TRUE(record.offered_hosts.empty());
    EXPECT_FALSE(was_offered(bystander));
}

TEST_F(SpecialProcedureTest, AMobThatRemovesTheOneActingEndsTheCommand)
{
    char_data* actor = make_mob_with("actor", nullptr);
    make_mob_with("guard", remove_actor);
    char_data* bystander = make_mob_with("bystander", record_offer);
    obj_data* lying = make_object_with(BREAD_VNUM, record_offer);
    obj_to_room(lying, 0);

    EXPECT_EQ(1, offer_command(actor));

    EXPECT_FALSE(is_in_game(actor));
    EXPECT_TRUE(record.offered_hosts.empty());
}

TEST_F(SpecialProcedureTest, ATargetThatRemovesTheOneActingEndsTheCommand)
{
    char_data* actor = make_mob_with("actor", nullptr);
    char_data* first_target = make_mob_with("first", remove_actor);
    char_data* second_target = make_mob_with("second", record_offer);
    waiting_type targets {};
    targets.targ1.set_character(first_target);
    targets.targ2.set_character(second_target);

    EXPECT_EQ(1, special(actor, NORTH + 1, empty_argument(), SPECIAL_TARGET, &targets));

    EXPECT_FALSE(is_in_game(actor));
    EXPECT_FALSE(was_offered(second_target));
}

TEST_F(SpecialProcedureTest, ATargetObjectTheFirstTargetRemovedIsNotOfferedTheCommand)
{
    char_data* first_target = make_mob_with("first", remove_marked_object);
    obj_data* second_target = make_object_with(RING_VNUM, record_offer);
    obj_to_room(second_target, 0);
    record.object_to_remove = second_target;
    waiting_type targets {};
    targets.targ1.set_character(first_target);
    targets.targ2.type = TARGET_OBJ;
    targets.targ2.ptr.obj = second_target;

    EXPECT_EQ(0, special(&m_player, NORTH + 1, empty_argument(), SPECIAL_TARGET, &targets));

    EXPECT_FALSE(is_in_object_list(second_target));
    EXPECT_TRUE(record.offered_hosts.empty());
}

TEST_F(SpecialProcedureTest, BothTargetsAreOfferedACommandNeitherTakes)
{
    char_data* first_target = make_mob_with("first", record_offer);
    obj_data* second_target = make_object_with(RING_VNUM, record_offer);
    obj_to_room(second_target, 0);
    waiting_type targets {};
    targets.targ1.set_character(first_target);
    targets.targ2.type = TARGET_OBJ;
    targets.targ2.ptr.obj = second_target;

    EXPECT_EQ(0, special(&m_player, NORTH + 1, empty_argument(), SPECIAL_TARGET, &targets));

    const std::vector<const void*> expected = { first_target, second_target };
    EXPECT_EQ(expected, record.offered_hosts);
}

/* A death procedure that removes its own mob ends the death there: there is no one left to die,
 * and no corpse. */
TEST_F(SpecialProcedureTest, ADeathProcedureThatRemovesTheDyingEndsTheDeath)
{
    char_data* dying = make_mob_with("dying", remove_own_mob);

    raw_kill(dying, nullptr, 0);

    EXPECT_FALSE(is_in_game(dying));
    EXPECT_EQ(nullptr, world[0].contents) << "no corpse";
}

TEST_F(SpecialProcedureTest, ADeathWithAProcedureThatLetsItGoOnLeavesACorpse)
{
    char_data* dying = make_mob_with("dying", record_offer);

    raw_kill(dying, nullptr, 0);

    EXPECT_FALSE(is_in_game(dying));
    EXPECT_NE(nullptr, world[0].contents) << "a corpse";
}

/* ---- the interpreter ---- */

/* The guard removes the one typing north; the north command and its targets' procedures are
 * then left alone. */
TEST_F(SpecialProcedureTest, ACommandWhoseProcedureRemovesTheOneTypingItGoesNoFurther)
{
    assign_command_pointers();
    make_mob_with("guard", remove_actor_going_north);
    char_data* walker = make_mob_with("walker", nullptr);
    char north[] = "north";

    command_interpreter(walker, north);

    EXPECT_FALSE(is_in_game(walker));
    EXPECT_EQ(nullptr, world[SECOND_ROOM].people);
}

TEST_F(SpecialProcedureTest, ACommandNoProcedureTakesIsCarriedOut)
{
    assign_command_pointers();
    make_mob_with("bystander", record_offer);
    char_data* walker = make_mob_with("walker", nullptr);
    char north[] = "north";

    command_interpreter(walker, north);

    ASSERT_TRUE(is_in_game(walker));
    EXPECT_EQ(SECOND_ROOM, walker->in_room);
}

/* ---- moving ---- */

/* A rider's own step is not offered to the procedures here, its mount's is: the guard removes
 * the mount, and the rider stays behind. */
TEST_F(SpecialProcedureTest, ARiderWhoseMountAGuardRemovesAsItSetsOffStaysBehind)
{
    make_mob_with("guard", remove_actor_going_north);
    char_data* mount = make_mob_with("horse", nullptr);
    char_data* rider = make_mob_with("rider", nullptr);
    seat(rider, mount);

    walk_north(rider);

    EXPECT_FALSE(is_in_game(mount));
    ASSERT_TRUE(is_in_game(rider));
    EXPECT_EQ(0, rider->in_room);
    EXPECT_FALSE(IS_RIDING(rider));
}

/* A follower's step is offered to the procedures here: the guard removes it, and nothing else
 * of the step happens. */
TEST_F(SpecialProcedureTest, AFollowerAGuardRemovesAsItSetsOffGoesNowhere)
{
    make_mob_with("guard", remove_actor_going_north);
    char_data* follower = make_mob_with("follower", nullptr);

    do_move(follower, empty_argument(), nullptr, NORTH + 1, SCMD_FOLLOW);

    EXPECT_FALSE(is_in_game(follower));
    EXPECT_EQ(nullptr, world[SECOND_ROOM].people);
}

/* The mount sets off on its own; the guard removes its only rider, and nobody is left to carry. */
TEST_F(SpecialProcedureTest, AMountWhoseRiderAGuardRemovesAsItSetsOffStays)
{
    make_mob_with("guard", remove_actor_going_north);
    char_data* mount = make_mob_with("horse", nullptr);
    char_data* rider = make_mob_with("rider", nullptr);
    seat(rider, mount);

    walk_north(mount);

    EXPECT_FALSE(is_in_game(rider));
    ASSERT_TRUE(is_in_game(mount));
    EXPECT_EQ(0, mount->in_room);
}

TEST_F(SpecialProcedureTest, AMountAGuardRemovesAsItsRiderSetsOffIsNotUsedAgain)
{
    make_mob_with("guard", remove_marked_going_north);
    char_data* mount = make_mob_with("horse", nullptr);
    char_data* rider = make_mob_with("rider", nullptr);
    seat(rider, mount);
    record.character_to_remove = mount;

    walk_north(mount);

    EXPECT_FALSE(is_in_game(mount));
    ASSERT_TRUE(is_in_game(rider));
    EXPECT_EQ(0, rider->in_room);
}

/* The guard where they arrive removes the mount: its riders arrive all the same. */
TEST_F(SpecialProcedureTest, AMountAGuardRemovesAsItArrivesIsLetGo)
{
    make_mob_with("guard", remove_marked_arriving, SECOND_ROOM);
    char_data* mount = make_mob_with("horse", nullptr);
    char_data* rider = make_mob_with("rider", nullptr);
    seat(rider, mount);
    record.character_to_remove = mount;

    walk_north(rider);

    EXPECT_FALSE(is_in_game(mount));
    ASSERT_TRUE(is_in_game(rider));
    EXPECT_EQ(SECOND_ROOM, rider->in_room);
}

TEST_F(SpecialProcedureTest, ARideIntoADeathRoomKillsEveryRiderAndTheMount)
{
    world[SECOND_ROOM].room_flags = DEATH;
    char_data* mount = make_mob_with("horse", nullptr);
    char_data* rider = make_mob_with("rider", nullptr);
    char_data* passenger = make_mob_with("passenger", nullptr);
    seat(rider, mount);
    seat(passenger, mount);

    walk_north(rider);

    EXPECT_FALSE(is_in_game(rider));
    EXPECT_FALSE(is_in_game(passenger));
    EXPECT_FALSE(is_in_game(mount));
    EXPECT_EQ(3, count_contents(world[SECOND_ROOM].contents)) << "three corpses";
}

/* The first rider's death removes the passenger behind it: the mount still goes on into the
 * death room. */
TEST_F(SpecialProcedureTest, ARiderWhoseDeathRemovesTheNextRiderLeavesTheMountToItsDeath)
{
    world[SECOND_ROOM].room_flags = DEATH;
    char_data* mount = make_mob_with("horse", nullptr);
    char_data* rider = make_mob_with("rider", remove_marked_on_death);
    char_data* passenger = make_mob_with("passenger", nullptr);
    seat(rider, mount);
    seat(passenger, mount);
    record.character_to_remove = passenger;

    walk_north(rider);

    EXPECT_FALSE(is_in_game(rider));
    EXPECT_FALSE(is_in_game(passenger));
    EXPECT_FALSE(is_in_game(mount));
}

/* The first rider's death removes the mount: the passenger, unseated with it, stays behind. */
TEST_F(SpecialProcedureTest, ARiderWhoseDeathRemovesTheMountLeavesThePassengerBehind)
{
    world[SECOND_ROOM].room_flags = DEATH;
    char_data* mount = make_mob_with("horse", nullptr);
    char_data* rider = make_mob_with("rider", remove_marked_on_death);
    char_data* passenger = make_mob_with("passenger", nullptr);
    seat(rider, mount);
    seat(passenger, mount);
    record.character_to_remove = mount;

    walk_north(rider);

    EXPECT_FALSE(is_in_game(rider));
    EXPECT_FALSE(is_in_game(mount));
    ASSERT_TRUE(is_in_game(passenger));
    EXPECT_EQ(0, passenger->in_room);
}

/* ---- mob turns ---- */

TEST_F(SpecialProcedureTest, AMobWhoseOwnProcedureRemovesItEndsItsTurn)
{
    char_data* vanishing = make_mob_with("vanishing", remove_own_mob);

    one_mobile_activity(vanishing);

    EXPECT_FALSE(is_in_game(vanishing));
}

TEST_F(SpecialProcedureTest, EveryMobTakesItsTurnWhenNoneIsRemoved)
{
    char_data* first = make_mob_with("first", record_offer);
    char_data* second = make_mob_with("second", record_offer);
    SET_BIT(MOB_FLAGS(first), MOB_SENTINEL);
    SET_BIT(MOB_FLAGS(second), MOB_SENTINEL);
    push_test_random_value(0.0); /* the first in the list takes its turn */
    push_test_random_value(0.0); /* and so does the second */

    mobile_activity();

    EXPECT_TRUE(was_offered(first));
    EXPECT_TRUE(was_offered(second));
}

/* The first mob in the list removes the one after it and then itself: the pass ends there. */
TEST_F(SpecialProcedureTest, AMobActivityPassEndsWhenATurnRemovesTheNextMobToo)
{
    char_data* next = make_mob_with("next", record_offer);
    char_data* remover = make_mob_with("remover", remove_marked_then_self);
    ASSERT_EQ(remover, character_list);
    ASSERT_EQ(next, remover->next);
    record.character_to_remove = next;
    push_test_random_value(0.0); /* the remover takes its turn */

    mobile_activity();

    EXPECT_FALSE(is_in_game(remover));
    EXPECT_FALSE(is_in_game(next));
}

/* Makes script SCRIPT_VNUM pause on hearing anything and then say <word>, and script
 * OTHER_SCRIPT_VNUM <reaction> when it hears <word>. The rest of a paused script names nobody
 * but its mob, so it reaches others through what it says. */
static std::vector<script_spec> pause_then_say_scripts(const char* word, const char* heard,
    std::vector<body_line> reaction)
{
    std::vector<body_line> listener = { { SCRIPT_IF_STR_CONTAINS, heard, { SCRIPT_PARAM_STR1 } },
        { SCRIPT_BEGIN, nullptr, {} } };
    listener.insert(listener.end(), reaction.begin(), reaction.end());
    listener.push_back({ SCRIPT_END, nullptr, {} });
    return { { SCRIPT_VNUM,
                 { { ON_HEAR_SAY,
                     { { SCRIPT_DO_WAIT, nullptr, { 5 } },
                         { SCRIPT_DO_SAY, word, { SCRIPT_PARAM_CH1 } } } } } },
        { OTHER_SCRIPT_VNUM, { { ON_HEAR_SAY, listener } } } };
}

/* The bodyguard rescues its master, which ends the master's pause: the rest of its script makes
 * the master's foe vanish, and the bodyguard does not then go after it. */
TEST_F(SpecialProcedureTest, ABodyguardWhoseRescueRemovesTheMastersFoeLetsItGo)
{
    install_scripts(pause_then_say_scripts(
        "vanish", "VANISH", { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } }));
    char_data* master = make_mob_with("master", nullptr);
    master->specials.script_number = SCRIPT_VNUM;
    char_data* foe = make_mob_with("foe", nullptr);
    foe->specials.script_number = OTHER_SCRIPT_VNUM;
    char_data* bodyguard = make_mob_with("bodyguard", nullptr);
    SET_BIT(MOB_FLAGS(bodyguard), MOB_BODYGUARD | MOB_SENTINEL);
    add_follower(bodyguard, master, FOLLOW_MOVE);
    run_command(do_say, "hello");
    ASSERT_EQ(CMD_SCRIPT, master->delay.cmd);
    start_fight(master, foe);
    push_test_random_value(0.0); /* the rescue succeeds (a mob knows it at 80) */

    one_mobile_activity(bodyguard);

    EXPECT_FALSE(is_in_game(foe));
    ASSERT_TRUE(is_in_game(bodyguard));
    EXPECT_EQ(nullptr, bodyguard->specials.fighting);
}

TEST_F(SpecialProcedureTest, ABodyguardRescuesEachFollowerInTrouble)
{
    char_data* bodyguard = make_mob_with("bodyguard", nullptr);
    SET_BIT(MOB_FLAGS(bodyguard), MOB_BODYGUARD | MOB_SENTINEL);
    char_data* first = make_mob_with("first", nullptr);
    char_data* second = make_mob_with("second", nullptr);
    add_follower(first, bodyguard, FOLLOW_MOVE);
    add_follower(second, bodyguard, FOLLOW_MOVE);
    char_data* first_foe = make_mob_with("foe", nullptr);
    char_data* second_foe = make_mob_with("enemy", nullptr);
    start_fight(first, first_foe);
    start_fight(second, second_foe);
    push_test_random_value(0.0); /* the first rescue succeeds */
    push_test_random_value(0.0); /* and so does the second */

    one_mobile_activity(bodyguard);

    EXPECT_EQ(bodyguard, first_foe->specials.fighting);
    EXPECT_EQ(bodyguard, second_foe->specials.fighting);
}

/* The bodyguard rescues a follower, which ends the follower's pause: the rest of its script
 * makes a stranger take the follower on and then vanish, so that the follower follows nobody.
 * The rescues end there: the follower's place in the bodyguard's list is gone. Before the rescue
 * another mob that followed someone was removed, so a place in the list that has gone leads on
 * to one naming a removed mob. */
TEST_F(SpecialProcedureTest, ABodyguardStopsRescuingOnceTheRescuedStopsFollowingIt)
{
    install_scripts(pause_then_say_scripts("leave", "LEAVE",
        { { SCRIPT_DO_FOLLOW, nullptr, { SCRIPT_PARAM_CH2, SCRIPT_PARAM_CH1 } },
            { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } }));
    char_data* bodyguard = make_mob_with("bodyguard", nullptr);
    SET_BIT(MOB_FLAGS(bodyguard), MOB_BODYGUARD | MOB_SENTINEL);
    char_data* follower = make_mob_with("follower", nullptr);
    follower->specials.script_number = SCRIPT_VNUM;
    add_follower(follower, bodyguard, FOLLOW_MOVE);
    char_data* stranger = make_mob_with("stranger", nullptr);
    stranger->specials.script_number = OTHER_SCRIPT_VNUM;
    char_data* foe = make_mob_with("foe", nullptr);
    char_data* stray = make_mob_with("stray", nullptr);
    add_follower(stray, foe, FOLLOW_MOVE);
    extract_char(stray);
    run_command(do_say, "hello");
    ASSERT_EQ(CMD_SCRIPT, follower->delay.cmd);
    start_fight(follower, foe);
    push_test_random_value(0.0); /* the rescue succeeds */

    one_mobile_activity(bodyguard);

    EXPECT_FALSE(is_in_game(stranger));
    ASSERT_TRUE(is_in_game(follower));
    EXPECT_EQ(nullptr, follower->master);
    EXPECT_EQ(nullptr, bodyguard->followers);
    EXPECT_EQ(bodyguard, foe->specials.fighting) << "the rescue itself went ahead";
}

/* ---- the dragon's breath ---- */

/* Makes <breather> a fighting dragon whose breath comes this turn, with the player standing aside
 * (a player burnt to death would be saved to disk). */
static void ready_to_breathe(char_data& player, char_data* breather)
{
    world[0].people = player.next_in_room;
    breather->specials.position = POSITION_FIGHTING;
    push_test_random_value(0.0); /* it breathes this turn */
}

/* Whether <victim> has lost hit points. */
static bool is_hurt(const char_data* victim) { return victim->tmpabilities.hit < victim->abilities.hit; }

TEST_F(SpecialProcedureTest, TheBreathHurtsEveryoneInTheRoomButTheDragon)
{
    char_data* breather = make_mob_with("dragon", nullptr);
    char_data* first = make_mob_with("first", nullptr);
    char_data* second = make_mob_with("second", nullptr);
    ready_to_breathe(m_player, breather);

    dragon(breather, breather, 0, empty_argument(), SPECIAL_SELF, nullptr);

    EXPECT_TRUE(is_hurt(first));
    EXPECT_TRUE(is_hurt(second));
    EXPECT_FALSE(is_hurt(breather));
}

/* The first victim's damage script makes the one after it vanish: the breath goes on to the one
 * after that. */
TEST_F(SpecialProcedureTest, TheBreathGoesOnPastAVictimItsNeighbourRemoved)
{
    install_scripts({ { SCRIPT_VNUM,
                          { { ON_DAMAGE, { { SCRIPT_DO_SAY, "vanish", { SCRIPT_PARAM_CH1 } } } } } },
        { OTHER_SCRIPT_VNUM,
            { { ON_HEAR_SAY,
                { { SCRIPT_IF_STR_CONTAINS, "VANISH", { SCRIPT_PARAM_STR1 } },
                    { SCRIPT_BEGIN, nullptr, {} },
                    { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } },
                    { SCRIPT_END, nullptr, {} } } } } } });
    char_data* breather = make_mob_with("dragon", nullptr);
    char_data* first = make_mob_with("first", nullptr);
    first->specials.script_number = SCRIPT_VNUM;
    char_data* second = make_mob_with("second", nullptr);
    second->specials.script_number = OTHER_SCRIPT_VNUM;
    char_data* third = make_mob_with("third", nullptr);
    ready_to_breathe(m_player, breather);

    dragon(breather, breather, 0, empty_argument(), SPECIAL_SELF, nullptr);

    EXPECT_TRUE(is_hurt(first));
    EXPECT_FALSE(is_in_game(second));
    EXPECT_TRUE(is_hurt(third));
}

/* The first victim's damage script removes the dragon: nobody after it is breathed on. */
TEST_F(SpecialProcedureTest, TheBreathEndsWithADragonAVictimRemoves)
{
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    char_data* breather = make_mob_with("dragon", nullptr);
    char_data* first = make_mob_with("first", nullptr);
    first->specials.script_number = SCRIPT_VNUM;
    char_data* second = make_mob_with("second", nullptr);
    ready_to_breathe(m_player, breather);

    dragon(breather, breather, 0, empty_argument(), SPECIAL_SELF, nullptr);

    EXPECT_FALSE(is_in_game(breather));
    EXPECT_FALSE(is_hurt(second));
}

/* ---- procedures that speak ---- */

/* Makes script SCRIPT_VNUM remove whoever speaks within earshot (ch2). */
static std::vector<body_line> remove_the_speaker_body()
{
    return { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } };
}

/* A gatekeeper that ends its delay with nothing to do says so; the hearer's script removes it on
 * hearing it, and its delay is then left alone. */
TEST_F(SpecialProcedureTest, AGatekeeperTheHearersRemoveAsItSpeaksIsLetGo)
{
    install_script(ON_HEAR_SAY, remove_the_speaker_body());
    for (special_func procedure : { gatekeeper, gatekeeper2 }) {
        char_data* keeper = make_mob_with("gatekeeper", nullptr);
        char_data* hearer = make_mob_with("hearer", nullptr);
        hearer->specials.script_number = SCRIPT_VNUM;

        EXPECT_FALSE(procedure(keeper, &m_player, -1, empty_argument(), SPECIAL_DELAY, nullptr));

        EXPECT_FALSE(is_in_game(keeper));
        extract_char(hearer);
    }
}

TEST_F(SpecialProcedureTest, AGatekeeperWithNothingToOpenSaysSoAndForgetsTheRequest)
{
    for (special_func procedure : { gatekeeper, gatekeeper2 }) {
        char_data* keeper = make_mob_with("gatekeeper", nullptr);
        keeper->delay.cmd = -1;
        keeper->delay.subcmd = 1;
        keeper->delay.targ2.ch_num = -1; /* nothing like that here */

        EXPECT_FALSE(procedure(keeper, &m_player, -1, empty_argument(), SPECIAL_DELAY, nullptr));

        EXPECT_EQ(0, keeper->delay.cmd);
        EXPECT_EQ(0, keeper->delay.subcmd);
        extract_char(keeper);
    }
    EXPECT_EQ(2, count_in_output("There is nothing like that here.")) << output();
}

/* Thuringwethil, beaten, says her last words; the hearer's script removes her on hearing them,
 * and she is not touched again. */
TEST_F(SpecialProcedureTest, ThuringwethilTheHearersRemoveAsSheLeavesIsLetGo)
{
    install_script(ON_HEAR_SAY, remove_the_speaker_body());
    char_data* vampire = make_mob_with("thuringwethil", nullptr);
    vampire->tmpabilities.hit = 400; /* beaten */
    char_data* hearer = make_mob_with("hearer", nullptr);
    hearer->specials.script_number = SCRIPT_VNUM;

    EXPECT_EQ(1, thuringwethil(vampire, vampire, 0, empty_argument(), SPECIAL_SELF, nullptr));

    EXPECT_FALSE(is_in_game(vampire));
    EXPECT_FALSE(output_has("her clothing falls to the ground")) << output();
}

TEST_F(SpecialProcedureTest, ThuringwethilBeatenSaysHerLastWordsAndLeaves)
{
    char_data* vampire = make_mob_with("thuringwethil", nullptr);
    vampire->tmpabilities.hit = 400; /* beaten */

    EXPECT_EQ(1, thuringwethil(vampire, vampire, 0, empty_argument(), SPECIAL_SELF, nullptr));

    EXPECT_FALSE(is_in_game(vampire));
    EXPECT_TRUE(output_has("Curse you all")) << output();
    EXPECT_TRUE(output_has("her clothing falls to the ground")) << output();
}
