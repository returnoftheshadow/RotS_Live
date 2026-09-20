#include "../char_utils.h"
#include "../structs.h"
#include "../utils.h"

#include "../interpre.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

extern struct room_data world;
extern int top_of_world;

char_data* find_most_hurt_rescuable_follower(char_data* leader);
ACMD(do_rescue);

namespace {

void ensure_test_world_room(int room_number)
{
    if (room_data::BASE_WORLD == nullptr)
        world.create_bulk(1);

    top_of_world = 0;
    world[0].number = room_number;
    world[0].people = nullptr;
}

/*
 * Builds the leader plus a room population, wiring `people` and `next_in_room`
 * in the order characters are added, the way char_to_room would.
 */
struct RescueRoom {
    char_data leader {};
    char_data* first = nullptr;

    RescueRoom()
    {
        ensure_test_world_room(3001);
        leader.in_room = 0;
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
