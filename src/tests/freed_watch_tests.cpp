/* freed_watch, which tells code that set off a script whether an object or character it holds was
 * freed meanwhile, the helpers a loop over a room uses to carry on after a trigger, and freed_log,
 * which records everything freed while it is in scope, and death_watch, which tells whether a
 * character died, though it lives on. */
#include "../freed_watch.h"

#include "scripted_room_fixture.h"

#include "../spells.h"

using namespace scripted_room;

namespace {

class FreedWatchTest : public ScriptedRoomFixture {
};

} // namespace

TEST_F(FreedWatchTest, AWatchReportsAnObjectFreedOnceItIsExtracted)
{
    obj_data* stone = make_object(STONE_VNUM, 0);
    freed_watch stone_watch(stone);

    extract_obj(stone);

    EXPECT_TRUE(stone_watch.freed());
}

TEST_F(FreedWatchTest, AWatchReportsNothingWhileItsObjectExists)
{
    obj_data* stone = make_object(STONE_VNUM, 0);
    obj_data* other_stone = make_object(STONE_VNUM, 0);
    freed_watch stone_watch(stone);

    extract_obj(other_stone);

    EXPECT_FALSE(stone_watch.freed());
}

TEST_F(FreedWatchTest, EveryWatchOnAnObjectIsMarked)
{
    obj_data* stone = make_object(STONE_VNUM, 0);
    freed_watch outer_watch(stone);
    freed_watch inner_watch(stone);

    extract_obj(stone);

    EXPECT_TRUE(outer_watch.freed());
    EXPECT_TRUE(inner_watch.freed());
}

/* Objects inside a container are freed with it. */
TEST_F(FreedWatchTest, AWatchReportsAnObjectFreedWithItsContainer)
{
    obj_data* bag = make_object(BAG_VNUM, 0);
    obj_data* stone = make_object(STONE_VNUM, 0);
    obj_to_obj(stone, bag);
    freed_watch stone_watch(stone);

    extract_obj(bag);

    EXPECT_TRUE(stone_watch.freed());
}

TEST_F(FreedWatchTest, AWatchReportsAMobFreedOnceItIsExtracted)
{
    char_data* mob = make_mob("mob", 0);
    freed_watch mob_watch(mob);

    extract_char(mob);

    EXPECT_TRUE(mob_watch.freed());
}

TEST_F(FreedWatchTest, AWatchOnNothingIsNeverFreed)
{
    const obj_data* no_object = nullptr;
    freed_watch empty_watch(no_object);

    extract_obj(make_object(STONE_VNUM, 0));

    EXPECT_FALSE(empty_watch.freed());
}

TEST_F(FreedWatchTest, AWatchOnACharacterIsNotMarkedByAnObject)
{
    char_data* mob = make_mob("mob", 0);
    freed_watch mob_watch(mob);

    extract_obj(make_object(STONE_VNUM, 0));

    EXPECT_FALSE(mob_watch.freed());
}

/* ---- going on with a loop over a room after a trigger ---- */

TEST_F(FreedWatchTest, ALoopGoesOnFromAPersonStillInTheRoom)
{
    char_data* first = make_mob("first", 0);
    char_data* second = make_mob("second", 0);

    EXPECT_EQ(second, next_person_after_trigger(world[0], first, false, second));
}

TEST_F(FreedWatchTest, ALoopGoesOnFromTheSavedNextPersonAfterOneWasFreed)
{
    char_data* first = make_mob("first", 0);
    char_data* second = make_mob("second", 0);
    extract_char(first);

    EXPECT_EQ(second, next_person_after_trigger(world[0], first, true, second));
}

/* One that left the room is followed as it always has been, along its own next_in_room. */
TEST_F(FreedWatchTest, ALoopFollowsAPersonThatLeftTheRoom)
{
    char_data* first = make_mob("first", 0);
    char_data* second = make_mob("second", 0);
    char_from_room(first);
    char_to_room(first, 0); /* back in, now after the second */

    EXPECT_EQ(nullptr, next_person_after_trigger(world[0], first, false, second));
}

TEST_F(FreedWatchTest, ALoopEndsWhenThePersonAndTheSavedNextAreBothGone)
{
    char_data* first = make_mob("first", 0);
    char_data* second = make_mob("second", 0);
    extract_char(second);
    extract_char(first);

    EXPECT_EQ(nullptr, next_person_after_trigger(world[0], first, true, second));
}

TEST_F(FreedWatchTest, ALoopGoesOnFromTheSavedNextObjectAfterOneWasFreed)
{
    obj_data* second = make_object(STONE_VNUM, 0);
    obj_to_room(second, 0);
    obj_data* first = make_object(STONE_VNUM, 0);
    obj_to_room(first, 0);
    extract_obj(first);

    EXPECT_EQ(second, next_content_after_trigger(world[0], first, true, second));
}

TEST_F(FreedWatchTest, ALoopGoesOnFromTheNextObjectOfOneStillInTheRoom)
{
    obj_data* second = make_object(STONE_VNUM, 0);
    obj_to_room(second, 0);
    obj_data* first = make_object(STONE_VNUM, 0);
    obj_to_room(first, 0);

    EXPECT_EQ(second, next_content_after_trigger(world[0], first, false, nullptr));
}

TEST_F(FreedWatchTest, AContentListHoldsOnlyItsOwnObjects)
{
    obj_data* carried = make_object(STONE_VNUM, 0);
    obj_to_char(carried, &m_player);
    obj_data* on_floor = make_object(STONE_VNUM, 0);
    obj_to_room(on_floor, 0);

    EXPECT_TRUE(is_on_content_list(m_player.carrying, carried));
    EXPECT_FALSE(is_on_content_list(m_player.carrying, on_floor));
    EXPECT_FALSE(is_on_content_list(m_player.carrying, nullptr));
}

/* ---- freed_log, which records everything freed while it is in scope ---- */

namespace {

/* An affect of <type> for a few hours, changing nothing. */
affected_type make_plain_affect(int type)
{
    affected_type affect {};
    affect.type = type;
    affect.duration = 5;
    affect.location = APPLY_NONE;
    return affect;
}

} // namespace

TEST_F(FreedWatchTest, ALogHoldsAnAffectRemovedWhileItIsInScope)
{
    affected_type armor = make_plain_affect(SPELL_ARMOR);
    affect_to_char(&m_player, &armor);
    affected_type* given = m_player.affected;
    freed_log log_in_scope;

    affect_remove(&m_player, given);

    EXPECT_TRUE(log_in_scope.contains(given));
}

TEST_F(FreedWatchTest, ALogHoldsNoAffectThatStillExists)
{
    affected_type armor = make_plain_affect(SPELL_ARMOR);
    affect_to_char(&m_player, &armor);
    affected_type sanctuary = make_plain_affect(SPELL_SANCTUARY);
    affect_to_char(&m_player, &sanctuary);
    affected_type* kept = m_player.affected;
    affected_type* removed = kept->next;
    freed_log log_in_scope;

    affect_remove(&m_player, removed);

    EXPECT_FALSE(log_in_scope.contains(kept));
    affect_remove(&m_player, kept);
}

/* The affected_list entry of a character goes with its last affect. */
TEST_F(FreedWatchTest, ALogHoldsTheListEntryOfACharactersLastAffect)
{
    extern universal_list* affected_list;
    universal_list* saved_affected_list = affected_list;
    affected_list = nullptr;
    affected_type armor = make_plain_affect(SPELL_ARMOR);
    affect_to_char(&m_player, &armor);
    universal_list* entry = affected_list;
    freed_log log_in_scope;

    affect_remove(&m_player, m_player.affected);

    EXPECT_TRUE(log_in_scope.contains(entry));
    affected_list = saved_affected_list;
}

TEST_F(FreedWatchTest, ALogHoldsAMobFreedWhileItIsInScope)
{
    char_data* mob = make_mob("mob", 0);
    freed_log log_in_scope;

    extract_char(mob);

    EXPECT_TRUE(log_in_scope.contains(mob));
}

/* Each log in scope records the free; one made after it does not. */
TEST_F(FreedWatchTest, OnlyLogsInScopeAtTheFreeHoldIt)
{
    char_data* mob = make_mob("mob", 0);
    freed_log outer_log;
    extract_char(mob);
    freed_log inner_log;

    EXPECT_TRUE(outer_log.contains(mob));
    EXPECT_FALSE(inner_log.contains(mob));
}

/* ---- death_watch ---- */

/* raw_kill reports each death (note_died); the login tests see a connected player die and live
 * on through one. */
TEST_F(FreedWatchTest, ADeathWatchSeesItsCharactersDeath)
{
    char_data* dying = make_mob("dying", 0);
    death_watch dying_watch(dying);

    death_watch::note_died(dying);

    EXPECT_TRUE(dying_watch.died());
}

TEST_F(FreedWatchTest, ADeathWatchSeesNobodyElsesDeath)
{
    char_data* bystander = make_mob("bystander", 0);
    death_watch player_death(&m_player);

    death_watch::note_died(bystander);

    EXPECT_FALSE(player_death.died());
}

TEST_F(FreedWatchTest, EveryDeathWatchOnACharacterIsMarked)
{
    death_watch outer_watch(&m_player);
    death_watch inner_watch(&m_player);

    death_watch::note_died(&m_player);

    EXPECT_TRUE(outer_watch.died());
    EXPECT_TRUE(inner_watch.died());
}

/* A watch that has gone out of scope is no longer reached: a death after it marks only the watches
 * still in scope. */
TEST_F(FreedWatchTest, ADeathWatchLeavingScopeLeavesTheOthersWatching)
{
    death_watch outer_watch(&m_player);
    {
        death_watch inner_watch(&m_player);
    }

    death_watch::note_died(&m_player);

    EXPECT_TRUE(outer_watch.died());
}
