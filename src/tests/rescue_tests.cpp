#include "../act_offe.h"
#include "../char_utils.h"
#include "../structs.h"
#include "../utils.h"

#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../spells.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

extern struct room_data world;
extern int top_of_world;

ACMD(do_rescue);

namespace {

/* Borrows room 0 of the shared test world for one test, and puts back what
 * it changed when the test ends, as the other suites using room 0 do. */
struct TestWorldRoom {
    int saved_top_of_world;
    int saved_number;
    char_data* saved_people;

    explicit TestWorldRoom(int room_number)
    {
        if (room_data::BASE_WORLD == nullptr)
            world.create_bulk(1);

        saved_top_of_world = top_of_world;
        saved_number = world[0].number;
        saved_people = world[0].people;

        top_of_world = 0;
        world[0].number = room_number;
        world[0].people = nullptr;
    }

    ~TestWorldRoom()
    {
        world[0].people = saved_people;
        world[0].number = saved_number;
        top_of_world = saved_top_of_world;
    }
};

/*
 * Builds the leader plus a room population, wiring `people` and `next_in_room`
 * in the order characters are added, the way char_to_room would.
 */
struct RescueRoom {
    TestWorldRoom world_room { 3001 };
    char_data leader {};
    char_data* first = nullptr;

    RescueRoom()
    {
        leader.in_room = 0;
        /* Awake, so the leader can see their followers. */
        leader.specials.position = POSITION_STANDING;
        add_to_room(&leader);
    }

    void add_to_room(char_data* character)
    {
        character->in_room = 0;
        character->next_in_room = first;
        first = character;
        world[0].people = first;
    }

    /* Makes `pet` a charmed follower of the leader, standing in the room. */
    void add_charmed_follower(char_data* pet, follow_type* link, int hit, int max_hit)
    {
        SET_BIT(pet->specials2.act, MOB_ISNPC);
        SET_BIT(pet->specials.affected_by, AFF_CHARM);
        pet->master = &leader;
        GET_HIT(pet) = hit;
        GET_MAX_HIT(pet) = max_hit;
        add_to_room(pet);

        link->follower = pet;
        link->next = leader.followers;
        leader.followers = link;
    }

    /* Puts `attacker` in the room, swinging at `victim`. */
    void add_attacker(char_data* attacker, char_data* victim)
    {
        SET_BIT(attacker->specials2.act, MOB_ISNPC);
        attacker->specials.fighting = victim;
        add_to_room(attacker);
    }
};

} // namespace

TEST(RescueFollowerTarget, PicksTheFollowerWithTheLowestFractionOfHealthRemaining)
{
    RescueRoom room;
    char_data sturdy_pet {}, frail_pet {};
    char_data sturdy_attacker {}, frail_attacker {};
    follow_type sturdy_link {}, frail_link {};

    /* 200/1000 is a fifth left; 50/100 is half left, despite being fewer points. */
    room.add_charmed_follower(&sturdy_pet, &sturdy_link, 200, 1000);
    room.add_charmed_follower(&frail_pet, &frail_link, 50, 100);
    room.add_attacker(&sturdy_attacker, &sturdy_pet);
    room.add_attacker(&frail_attacker, &frail_pet);

    EXPECT_EQ(find_most_hurt_rescuable_follower(&room.leader), &sturdy_pet)
        << "Expected the follower closest to dying by proportion, not the one with fewer hit points.";
}

TEST(RescueFollowerTarget, SkipsAFollowerNobodyIsFightingEvenWhenItIsTheMostHurt)
{
    RescueRoom room;
    char_data bleeding_pet {}, engaged_pet {};
    char_data attacker {};
    follow_type bleeding_link {}, engaged_link {};

    room.add_charmed_follower(&bleeding_pet, &bleeding_link, 10, 1000);
    room.add_charmed_follower(&engaged_pet, &engaged_link, 900, 1000);
    room.add_attacker(&attacker, &engaged_pet);

    EXPECT_EQ(find_most_hurt_rescuable_follower(&room.leader), &engaged_pet)
        << "Expected an untouched but badly wounded follower to be passed over for the one actually under attack.";
}

TEST(RescueFollowerTarget, IgnoresAFollowerWhoIsNotCharmed)
{
    RescueRoom room;
    char_data companion {}, pet {};
    char_data companion_attacker {}, pet_attacker {};
    follow_type companion_link {}, pet_link {};

    /* A grouped player following you is not something 'rescue follower' targets. */
    room.add_charmed_follower(&companion, &companion_link, 10, 1000);
    REMOVE_BIT(companion.specials.affected_by, AFF_CHARM);
    room.add_charmed_follower(&pet, &pet_link, 900, 1000);
    room.add_attacker(&companion_attacker, &companion);
    room.add_attacker(&pet_attacker, &pet);

    EXPECT_EQ(find_most_hurt_rescuable_follower(&room.leader), &pet)
        << "Expected an uncharmed follower to be left out of the charmed-follower search.";
}

TEST(RescueFollowerTarget, IgnoresAFollowerStandingInAnotherRoom)
{
    RescueRoom room;
    char_data distant_pet {}, nearby_pet {};
    char_data stale_attacker {}, nearby_attacker {};
    follow_type distant_link {}, nearby_link {};

    room.add_charmed_follower(&distant_pet, &distant_link, 10, 1000);
    distant_pet.in_room = 0 + 1; /* wandered off; a stale fighting pointer remains behind */
    room.add_charmed_follower(&nearby_pet, &nearby_link, 900, 1000);
    room.add_attacker(&stale_attacker, &distant_pet);
    room.add_attacker(&nearby_attacker, &nearby_pet);

    EXPECT_EQ(find_most_hurt_rescuable_follower(&room.leader), &nearby_pet)
        << "Expected a follower outside the room to be unreachable, however badly hurt.";
}

TEST(RescueFollowerTarget, SurvivesAFollowerWhoseMaximumHitPointsAreZero)
{
    RescueRoom room;
    char_data broken_pet {}, healthy_pet {};
    char_data broken_attacker {}, healthy_attacker {};
    follow_type broken_link {}, healthy_link {};

    /* Bad mob data must not reach the integer divide and fault the server. */
    room.add_charmed_follower(&broken_pet, &broken_link, 0, 0);
    room.add_charmed_follower(&healthy_pet, &healthy_link, 900, 1000);
    room.add_attacker(&broken_attacker, &broken_pet);
    room.add_attacker(&healthy_attacker, &healthy_pet);

    EXPECT_EQ(find_most_hurt_rescuable_follower(&room.leader), &healthy_pet)
        << "Expected a follower with no maximum hit points to be skipped rather than divided by.";
}

TEST(RescueFollowerTarget, IncludesAGuardianAmongTheRescuableFollowers)
{
    RescueRoom room;
    char_data guardian {}, pet {};
    char_data guardian_attacker {}, pet_attacker {};
    follow_type guardian_link {}, pet_link {};

    /*
     * 'order' deliberately skips guardians because they take no orders, but the
     * rescuer is the one acting here -- a guardian being beaten on is exactly
     * the party member a player means to save.
     */
    room.add_charmed_follower(&guardian, &guardian_link, 100, 1000);
    SET_BIT(MOB_FLAGS(&guardian), MOB_GUARDIAN);
    room.add_charmed_follower(&pet, &pet_link, 900, 1000);
    room.add_attacker(&guardian_attacker, &guardian);
    room.add_attacker(&pet_attacker, &pet);

    ASSERT_TRUE(utils::is_guardian(guardian)) << "Test setup failed to produce a guardian.";
    EXPECT_EQ(find_most_hurt_rescuable_follower(&room.leader), &guardian)
        << "Expected a guardian to be rescuable like any other charmed follower.";
}

TEST(RescueFollowerTarget, ReportsNoTargetWhenNoFollowerIsUnderAttack)
{
    RescueRoom room;
    char_data pet {};
    follow_type pet_link {};

    room.add_charmed_follower(&pet, &pet_link, 500, 1000);

    EXPECT_EQ(find_most_hurt_rescuable_follower(&room.leader), nullptr)
        << "Expected no target so the caller can explain that nobody needs rescuing.";
}

TEST(RescueFollowerTarget, ResolvesEquallyHurtFollowersToTheFirstInTheFollowerList)
{
    RescueRoom room;
    char_data older_pet {}, newer_pet {};
    char_data older_attacker {}, newer_attacker {};
    follow_type older_link {}, newer_link {};

    /* Followers are prepended, so the most recently charmed pet leads the list. */
    room.add_charmed_follower(&older_pet, &older_link, 300, 1000);
    room.add_charmed_follower(&newer_pet, &newer_link, 300, 1000);
    room.add_attacker(&older_attacker, &older_pet);
    room.add_attacker(&newer_attacker, &newer_pet);

    ASSERT_EQ(room.leader.followers->follower, &newer_pet) << "Test setup assumed a prepended list.";
    EXPECT_EQ(find_most_hurt_rescuable_follower(&room.leader), &newer_pet)
        << "Expected a tie to resolve deterministically to the head of the follower list.";
}

namespace {

descriptor_data make_descriptor()
{
    descriptor_data descriptor {};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    return descriptor;
}

/* Types `rescue <argument>` the way the interpreter does: text that named no
 * character in the room arrives as a TARGET_TEXT wtl, never a TARGET_CHAR. */
std::string rescue_with_text_argument(char_data* leader, const char* argument)
{
    waiting_type wtl {};
    wtl.targ1.type = TARGET_TEXT;
    wtl.cmd = CMD_RESCUE;

    char writable[MAX_INPUT_LENGTH];
    strcpy(writable, argument);

    leader->desc->small_outbuf[0] = '\0';
    leader->desc->output = leader->desc->small_outbuf;
    leader->desc->bufptr = 0;
    leader->desc->bufspace = SMALL_BUFSIZE - 1;

    do_rescue(leader, writable, &wtl, CMD_RESCUE, 0);

    return std::string(leader->desc->small_outbuf);
}

} // namespace

TEST(RescueFollowerCommand, ExplainsThatNoFollowersArePresent)
{
    RescueRoom room;
    descriptor_data descriptor = make_descriptor();
    room.leader.desc = &descriptor;
    room.leader.player.name = const_cast<char*>("leader");
    room.leader.specials.position = POSITION_STANDING;

    const std::string output = rescue_with_text_argument(&room.leader, "follower");

    EXPECT_NE(output.find("None of your followers are here."), std::string::npos) << output;
    EXPECT_EQ(output.find("Alas!"), std::string::npos)
        << "Expected the follower keyword to be parsed rather than rejected as a lost victim: " << output;
}

TEST(RescueFollowerCommand, ExplainsThatNoFollowerNeedsRescuing)
{
    RescueRoom room;
    descriptor_data descriptor = make_descriptor();
    room.leader.desc = &descriptor;
    room.leader.player.name = const_cast<char*>("leader");
    room.leader.specials.position = POSITION_STANDING;

    char_data pet {};
    follow_type pet_link {};
    room.add_charmed_follower(&pet, &pet_link, 500, 1000);
    pet.player.name = const_cast<char*>("orc");

    const std::string output = rescue_with_text_argument(&room.leader, "follower");

    EXPECT_NE(output.find("None of your followers need rescuing."), std::string::npos) << output;
}

TEST(RescueFollowerCommand, StillAsksWhoWhenTheNameMatchesNobody)
{
    RescueRoom room;
    descriptor_data descriptor = make_descriptor();
    room.leader.desc = &descriptor;
    room.leader.player.name = const_cast<char*>("leader");
    room.leader.specials.position = POSITION_STANDING;

    const std::string output = rescue_with_text_argument(&room.leader, "zzzznobody");

    EXPECT_NE(output.find("Who do you want to rescue?"), std::string::npos) << output;
}

TEST(RescueFollowerCommand, AsksWhoWhenNoNameIsGiven)
{
    RescueRoom room;
    descriptor_data descriptor = make_descriptor();
    room.leader.desc = &descriptor;
    room.leader.player.name = const_cast<char*>("leader");
    room.leader.specials.position = POSITION_STANDING;

    /* However the interpreter fills the target in for a bare 'rescue' (no
     * target, or the typist themself), an empty argument must ask who. */
    for (int resolved_to_self = 0; resolved_to_self < 2; ++resolved_to_self) {
        waiting_type wtl {};
        wtl.cmd = CMD_RESCUE;
        if (resolved_to_self) {
            wtl.targ1.type = TARGET_CHAR;
            wtl.targ1.ptr.ch = &room.leader;
            wtl.targ1.ch_num = room.leader.abs_number;
        } else {
            wtl.targ1.type = TARGET_NONE;
        }
        char empty[] = "";
        descriptor.small_outbuf[0] = '\0';
        descriptor.bufptr = 0;
        do_rescue(&room.leader, empty, &wtl, CMD_RESCUE, 0);
        const std::string output = descriptor.small_outbuf;

        EXPECT_NE(output.find("Who do you want to rescue?"), std::string::npos)
            << "resolved_to_self=" << resolved_to_self << ": " << output;
        EXPECT_EQ(output.find("What about fleeing instead?"), std::string::npos)
            << "A bare 'rescue' must not be read as rescuing yourself: " << output;
    }
}

TEST(RescueFollowerCommand, StillReportsALostVictimWhenTheResolvedTargetIsGone)
{
    RescueRoom room;
    descriptor_data descriptor = make_descriptor();
    room.leader.desc = &descriptor;
    room.leader.player.name = const_cast<char*>("leader");
    room.leader.specials.position = POSITION_STANDING;

    char_data departed {};
    departed.in_room = room.leader.in_room + 1;

    waiting_type wtl {};
    wtl.targ1.type = TARGET_CHAR;
    wtl.targ1.ptr.ch = &departed;
    wtl.cmd = CMD_RESCUE;

    char writable[MAX_INPUT_LENGTH];
    strcpy(writable, "someone");
    do_rescue(&room.leader, writable, &wtl, CMD_RESCUE, 0);

    const std::string output = descriptor.small_outbuf;
    EXPECT_NE(output.find("Alas! You lost your victim."), std::string::npos)
        << "Expected a resolved-but-departed target to keep its original message: " << output;
}

extern char_data* combat_list;
extern char_data* waiting_list;

namespace {

/* A real rescue calls set_fighting and WAIT_STATE, which link these stack
 * characters into the global combat and waiting lists (WAIT_STATE appends to
 * the tail). Start each test from empty lists, so nothing an earlier test left
 * is ever linked to these characters, and put the saved lists back after. */
struct GlobalListsGuard {
    char_data* saved_combat_list = combat_list;
    char_data* saved_waiting_list = waiting_list;
    GlobalListsGuard()
    {
        combat_list = nullptr;
        waiting_list = nullptr;
    }
    ~GlobalListsGuard()
    {
        combat_list = saved_combat_list;
        waiting_list = saved_waiting_list;
    }
};

} // namespace

namespace {

/*
 * A pet standing beside its master, set up to pass the rescue roll every time
 * so the tests see where the rescue lands rather than whether it succeeded.
 */
struct PetBesideMaster {
    RescueRoom room;
    descriptor_data descriptor = make_descriptor();
    char_data pet {};
    follow_type pet_link {};
    byte pet_knowledge[MAX_SKILLS] {};
    GlobalListsGuard lists_guard;

    PetBesideMaster()
    {
        room.leader.player.name = const_cast<char*>("tamer");
        room.leader.specials.position = POSITION_FIGHTING;

        room.add_charmed_follower(&pet, &pet_link, 500, 1000);
        pet.player.name = const_cast<char*>("wolf");
        pet.specials.position = POSITION_STANDING;
        pet.desc = &descriptor;
        pet_knowledge[SKILL_RESCUE] = 101;
        pet.knowledge = pet_knowledge;
    }
};

} // namespace

TEST(RescueLeaderCommand, PetRescuesItsMasterWhenToldToRescueLeader)
{
    PetBesideMaster setup;
    char_data attacker {};
    setup.room.add_attacker(&attacker, &setup.room.leader);

    const std::string output = rescue_with_text_argument(&setup.pet, "leader");

    EXPECT_EQ(attacker.specials.fighting, &setup.pet)
        << "Expected the attacker to be pulled off the master onto the pet: " << output;
    EXPECT_EQ(setup.pet.specials.fighting, &attacker) << output;
}

TEST(RescueLeaderCommand, MasterIsAcceptedAsTheSameKeyword)
{
    PetBesideMaster setup;
    char_data attacker {};
    setup.room.add_attacker(&attacker, &setup.room.leader);

    const std::string output = rescue_with_text_argument(&setup.pet, "master");

    EXPECT_EQ(attacker.specials.fighting, &setup.pet)
        << "Expected 'master' to rescue the master just as 'leader' does: " << output;
}

TEST(RescueLeaderCommand, SaysSoWhenTheMasterIsNotInTheRoom)
{
    PetBesideMaster setup;
    setup.room.leader.in_room = setup.pet.in_room + 1;

    const std::string output = rescue_with_text_argument(&setup.pet, "leader");

    EXPECT_NE(output.find("Your leader isn't here."), std::string::npos) << output;
}

TEST(RescueLeaderCommand, SaysSoWhenThereIsNoMaster)
{
    PetBesideMaster setup;
    setup.pet.master = nullptr;

    const std::string output = rescue_with_text_argument(&setup.pet, "leader");

    EXPECT_NE(output.find("Your leader isn't here."), std::string::npos) << output;
}

TEST(RescueLeaderCommand, ExplainsWhenNobodyIsFightingTheMaster)
{
    PetBesideMaster setup;

    const std::string output = rescue_with_text_argument(&setup.pet, "leader");

    EXPECT_NE(output.find("But no mortal is fighting"), std::string::npos)
        << "Expected the keyword to resolve to the master and then fail the usual way: " << output;
}

TEST(RescueLeaderCommand, TreatsAnUnseenMasterAsAbsent)
{
    PetBesideMaster setup;
    char_data attacker {};
    setup.room.add_attacker(&attacker, &setup.room.leader);
    SET_BIT(setup.room.leader.specials.affected_by, AFF_INVISIBLE);

    const std::string output = rescue_with_text_argument(&setup.pet, "leader");

    EXPECT_NE(output.find("Your leader isn't here."), std::string::npos) << output;
    EXPECT_EQ(attacker.specials.fighting, &setup.room.leader)
        << "A rescuer who cannot see its leader must not step in for them: " << output;
}

extern command_info cmd_info[];
void assign_command_pointers(void);

/* 'rescue leader' / 'master' / 'followers' only reach the keyword code when the
 * word names nobody, so rescue must never look for a character outside the room:
 * any visible mob called 'leader' or 'master' anywhere in the game would win. */
TEST(RescueCommandRegistration, LooksForTheTargetOnlyInTheRoom)
{
    assign_command_pointers();

    int char_room_index = -1, char_world_index = -1;
    for (int index = 0; index < 32; index++) {
        if ((1 << index) == TAR_CHAR_ROOM)
            char_room_index = index;
        if ((1 << index) == TAR_CHAR_WORLD)
            char_world_index = index;
    }
    ASSERT_GE(char_room_index, 0);
    ASSERT_GE(char_world_index, 0);

    EXPECT_NE(cmd_info[CMD_RESCUE].target_mask[char_room_index], 0);
    EXPECT_EQ(cmd_info[CMD_RESCUE].target_mask[char_world_index], 0);
}

TEST(RescueFollowerTarget, PassesOverAFollowerTheLeaderCannotSee)
{
    RescueRoom room;

    char_data unseen_pet {}, seen_pet {};
    follow_type unseen_link {}, seen_link {};
    room.add_charmed_follower(&unseen_pet, &unseen_link, 100, 1000);
    room.add_charmed_follower(&seen_pet, &seen_link, 600, 1000);
    SET_BIT(unseen_pet.specials.affected_by, AFF_INVISIBLE);

    char_data attacker_one {}, attacker_two {};
    room.add_attacker(&attacker_one, &unseen_pet);
    room.add_attacker(&attacker_two, &seen_pet);

    EXPECT_EQ(find_most_hurt_rescuable_follower(&room.leader), &seen_pet)
        << "An invisible pet must be passed over even though it is the most hurt.";
}

TEST(RescueFollowerCommand, SaysNoneAreHereWhenTheOnlyFollowerIsUnseen)
{
    RescueRoom room;
    descriptor_data descriptor = make_descriptor();
    room.leader.desc = &descriptor;
    room.leader.player.name = const_cast<char*>("leader");
    room.leader.specials.position = POSITION_STANDING;

    char_data pet {};
    follow_type pet_link {};
    room.add_charmed_follower(&pet, &pet_link, 100, 1000);
    pet.player.name = const_cast<char*>("wolf");
    SET_BIT(pet.specials.affected_by, AFF_INVISIBLE);
    char_data attacker {};
    room.add_attacker(&attacker, &pet);

    const std::string output = rescue_with_text_argument(&room.leader, "followers");

    EXPECT_NE(output.find("None of your followers are here."), std::string::npos) << output;
    EXPECT_EQ(attacker.specials.fighting, &pet) << output;
}

namespace {

/* Runs do_rescue the way the interpreter does when the typed word matched a
 * character in the room: that character arrives as a TARGET_CHAR wtl. */
std::string rescue_with_resolved_target(char_data* rescuer, char_data* resolved, const char* argument, int cmd)
{
    waiting_type wtl {};
    wtl.targ1.type = TARGET_CHAR;
    /* The interpreter only resolves characters that exist in the game. */
    const int slot = 4242;
    resolved->abs_number = slot;
    set_char_exists(slot);

    wtl.targ1.ptr.ch = resolved;
    wtl.targ1.ch_num = resolved->abs_number;
    wtl.cmd = CMD_RESCUE;

    char writable[MAX_INPUT_LENGTH];
    strcpy(writable, argument);

    if (rescuer->desc) {
        rescuer->desc->small_outbuf[0] = '\0';
        rescuer->desc->bufptr = 0;
    }
    do_rescue(rescuer, writable, &wtl, cmd, 0);
    remove_char_exists(slot);

    return rescuer->desc ? std::string(rescuer->desc->small_outbuf) : std::string();
}

} // namespace

/* 'order followers rescue leader' in a fight with an 'orc leader': the word
 * also names the enemy, but the pet must save its master, not the orc. */
TEST(RescueLeaderCommand, KeywordBeatsAnEnemyCalledLeader)
{
    PetBesideMaster setup;
    char_data orc {};
    setup.room.add_attacker(&orc, &setup.room.leader);
    orc.player.name = const_cast<char*>("orc leader");
    setup.room.leader.specials.fighting = &orc;

    const std::string output = rescue_with_resolved_target(&setup.pet, &orc, "leader", CMD_RESCUE);

    EXPECT_EQ(orc.specials.fighting, &setup.pet)
        << "Expected the orc to be pulled off the master onto the pet: " << output;
    EXPECT_NE(setup.room.leader.specials.fighting, &setup.pet)
        << "The pet must never end up fighting its own master: " << output;
}

TEST(RescueFollowerCommand, KeywordBeatsACharacterTheWordAlsoNames)
{
    GlobalListsGuard lists_guard;
    RescueRoom room;
    descriptor_data descriptor = make_descriptor();
    room.leader.desc = &descriptor;
    room.leader.player.name = const_cast<char*>("leader");

    char_data pet {};
    follow_type pet_link {};
    room.add_charmed_follower(&pet, &pet_link, 100, 1000);
    pet.player.name = const_cast<char*>("wolf");
    char_data pet_attacker {};
    room.add_attacker(&pet_attacker, &pet);

    char_data stranger {};
    room.add_to_room(&stranger);
    stranger.player.name = const_cast<char*>("followers");
    char_data stranger_attacker {};
    room.add_attacker(&stranger_attacker, &stranger);

    byte knowledge[MAX_SKILLS] {};
    knowledge[SKILL_RESCUE] = 101;
    room.leader.knowledge = knowledge;

    const std::string output = rescue_with_resolved_target(&room.leader, &stranger, "followers", CMD_RESCUE);

    EXPECT_EQ(pet_attacker.specials.fighting, &room.leader) << output;
    EXPECT_EQ(stranger_attacker.specials.fighting, &stranger) << output;
}

TEST(RescueLeaderCommand, NameStillWinsWhenTheRescuerFollowsNobody)
{
    GlobalListsGuard lists_guard;
    RescueRoom room;
    descriptor_data descriptor = make_descriptor();
    room.leader.desc = &descriptor;
    room.leader.player.name = const_cast<char*>("hero");

    char_data captain {};
    room.add_to_room(&captain);
    captain.player.name = const_cast<char*>("leader captain");
    char_data attacker {};
    room.add_attacker(&attacker, &captain);

    byte knowledge[MAX_SKILLS] {};
    knowledge[SKILL_RESCUE] = 101;
    room.leader.knowledge = knowledge;

    const std::string output = rescue_with_resolved_target(&room.leader, &captain, "leader", CMD_RESCUE);

    EXPECT_EQ(attacker.specials.fighting, &room.leader)
        << "With no leader to follow, 'leader' is just a name: " << output;
}

/* Mob bodyguards (mobact.cpp) call do_rescue directly with cmd 0, a resolved
 * target and that target's first name -- which may well be 'master'. */
TEST(RescueLeaderCommand, BodyguardCallKeepsItsResolvedTarget)
{
    PetBesideMaster setup;
    char_data ward {};
    setup.room.add_to_room(&ward);
    ward.player.name = const_cast<char*>("master lake town man");
    char_data ward_attacker {}, master_attacker {};
    setup.room.add_attacker(&ward_attacker, &ward);
    setup.room.add_attacker(&master_attacker, &setup.room.leader);

    rescue_with_resolved_target(&setup.pet, &ward, "master", 0);

    EXPECT_EQ(ward_attacker.specials.fighting, &setup.pet);
    EXPECT_EQ(master_attacker.specials.fighting, &setup.room.leader);
}

/* The master is here but unseen (invisible tamer) while an 'orc leader' is
 * in the fight: 'leader' still means the master, so the pet must not fall
 * back to the orc's name and turn on its own master. */
TEST(RescueLeaderCommand, UnseenMasterNeverFallsBackToAnEnemyCalledLeader)
{
    PetBesideMaster setup;
    char_data orc {};
    setup.room.add_attacker(&orc, &setup.room.leader);
    orc.player.name = const_cast<char*>("orc leader");
    setup.room.leader.specials.fighting = &orc;
    SET_BIT(setup.room.leader.specials.affected_by, AFF_INVISIBLE);

    const std::string output = rescue_with_resolved_target(&setup.pet, &orc, "leader", CMD_RESCUE);

    EXPECT_NE(output.find("Your leader isn't here."), std::string::npos) << output;
    EXPECT_EQ(orc.specials.fighting, &setup.room.leader) << output;
    EXPECT_NE(setup.room.leader.specials.fighting, &setup.pet)
        << "The pet must never end up fighting its own master: " << output;
}

TEST(RescueFollowerCommand, UnseenFollowerNeverFallsBackToACharacterTheWordNames)
{
    GlobalListsGuard lists_guard;
    RescueRoom room;
    descriptor_data descriptor = make_descriptor();
    room.leader.desc = &descriptor;
    room.leader.player.name = const_cast<char*>("leader");

    char_data pet {};
    follow_type pet_link {};
    room.add_charmed_follower(&pet, &pet_link, 100, 1000);
    SET_BIT(pet.specials.affected_by, AFF_INVISIBLE);

    char_data stranger {};
    room.add_to_room(&stranger);
    stranger.player.name = const_cast<char*>("followers");
    char_data stranger_attacker {};
    room.add_attacker(&stranger_attacker, &stranger);

    byte knowledge[MAX_SKILLS] {};
    knowledge[SKILL_RESCUE] = 101;
    room.leader.knowledge = knowledge;

    const std::string output = rescue_with_resolved_target(&room.leader, &stranger, "followers", CMD_RESCUE);

    EXPECT_NE(output.find("None of your followers are here."), std::string::npos) << output;
    EXPECT_EQ(stranger_attacker.specials.fighting, &stranger) << output;
}

/* A quoted word must mean the same as an unquoted one: the target parser
 * strips the quotes, so the keyword check has to as well. */
TEST(RescueLeaderCommand, QuotedKeywordStillBeatsAnEnemyCalledLeader)
{
    PetBesideMaster setup;
    char_data orc {};
    setup.room.add_attacker(&orc, &setup.room.leader);
    orc.player.name = const_cast<char*>("orc leader");
    setup.room.leader.specials.fighting = &orc;

    /* The word buffer is shared; make sure no earlier command's letters
     * linger in it to make this pass by accident. */
    strcpy(arg, "zzz");
    const std::string output = rescue_with_resolved_target(&setup.pet, &orc, "'leader'", CMD_RESCUE);

    EXPECT_EQ(orc.specials.fighting, &setup.pet)
        << "Expected the orc to be pulled off the master onto the pet: " << output;
    EXPECT_NE(setup.room.leader.specials.fighting, &setup.pet)
        << "The pet must never end up fighting its own master: " << output;
}
