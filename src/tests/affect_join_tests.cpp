// affect_join, which merges a new affect into one of the same type the character already has
// (a poison landing on someone already poisoned) or else adds it.
#include "scripted_room_fixture.h"

#include "../spells.h"

using namespace scripted_room;

namespace {

// The duration and modifier of the poison the player already has.
constexpr int OLD_POISON_DURATION = 10;
constexpr int OLD_POISON_MODIFIER = 4;
// The duration and modifier of a new poison weaker than the old one, which adds the old's.
constexpr int NEW_POISON_DURATION = 3;
constexpr int NEW_POISON_MODIFIER = 1;
// The duration and modifier of a new poison stronger than the old one, which keeps its own.
constexpr int STRONGER_POISON_DURATION = OLD_POISON_DURATION + 5;
constexpr int STRONGER_POISON_MODIFIER = OLD_POISON_MODIFIER + 2;
// The duration and modifier of a poison older than the old one, further down the list.
constexpr int OLDER_POISON_DURATION = 7;
constexpr int OLDER_POISON_MODIFIER = 2;
// The duration and modifier of the player's affects of other types.
constexpr int OTHER_AFFECT_DURATION = 5;
constexpr int OTHER_AFFECT_MODIFIER = 0;

// Joins affects onto the fixture's player, whose affects TearDown removes.
class AffectJoinTest : public ScriptedRoomFixture {
protected:
    void TearDown() override
    {
        while (m_player.affected) {
            affect_remove(&m_player, m_player.affected);
        }
        ScriptedRoomFixture::TearDown();
    }

    // An affect of <type> for <duration> hours with <modifier>, changing nothing.
    static affected_type make_affect(int type, int duration, int modifier)
    {
        affected_type affect {};
        affect.type = type;
        affect.duration = duration;
        affect.modifier = modifier;
        affect.location = APPLY_NONE;
        return affect;
    }

    // How many affects of <type> the player has.
    int count_player_affects(int type) const
    {
        int count = 0;
        for (const affected_type* affect = m_player.affected; affect; affect = affect->next) {
            if (affect->type == type) {
                ++count;
            }
        }
        return count;
    }
};

} // namespace

TEST_F(AffectJoinTest, MergesANewPoisonIntoTheOneAlreadyThere)
{
    affected_type old_poison = make_affect(SPELL_POISON, OLD_POISON_DURATION, OLD_POISON_MODIFIER);
    affect_to_char(&m_player, &old_poison);
    affected_type new_poison = make_affect(SPELL_POISON, NEW_POISON_DURATION, NEW_POISON_MODIFIER);

    affect_join(&m_player, &new_poison, FALSE, FALSE);

    ASSERT_EQ(1, count_player_affects(SPELL_POISON));
    const affected_type* merged = affected_by_spell(&m_player, SPELL_POISON);
    EXPECT_EQ(NEW_POISON_DURATION + OLD_POISON_DURATION, merged->duration);
    EXPECT_EQ(NEW_POISON_MODIFIER + OLD_POISON_MODIFIER, merged->modifier);
}

// A longer or stronger new poison keeps its own duration or modifier instead of adding the old.
TEST_F(AffectJoinTest, AStrongerNewPoisonKeepsItsOwnValues)
{
    affected_type old_poison = make_affect(SPELL_POISON, OLD_POISON_DURATION, OLD_POISON_MODIFIER);
    affect_to_char(&m_player, &old_poison);
    affected_type new_poison
        = make_affect(SPELL_POISON, STRONGER_POISON_DURATION, STRONGER_POISON_MODIFIER);

    affect_join(&m_player, &new_poison, FALSE, FALSE);

    ASSERT_EQ(1, count_player_affects(SPELL_POISON));
    const affected_type* merged = affected_by_spell(&m_player, SPELL_POISON);
    EXPECT_EQ(STRONGER_POISON_DURATION, merged->duration);
    EXPECT_EQ(STRONGER_POISON_MODIFIER, merged->modifier);
}

// The merged affect is the newest, at the head of the list; the others keep their order.
TEST_F(AffectJoinTest, PutsTheMergedAffectFirstAndKeepsTheOthers)
{
    affected_type armor = make_affect(SPELL_ARMOR, OTHER_AFFECT_DURATION, OTHER_AFFECT_MODIFIER);
    affect_to_char(&m_player, &armor);
    affected_type old_poison = make_affect(SPELL_POISON, OLD_POISON_DURATION, OLD_POISON_MODIFIER);
    affect_to_char(&m_player, &old_poison);
    affected_type sanctuary
        = make_affect(SPELL_SANCTUARY, OTHER_AFFECT_DURATION, OTHER_AFFECT_MODIFIER);
    affect_to_char(&m_player, &sanctuary);
    affected_type new_poison = make_affect(SPELL_POISON, NEW_POISON_DURATION, NEW_POISON_MODIFIER);

    affect_join(&m_player, &new_poison, FALSE, FALSE);

    ASSERT_NE(nullptr, m_player.affected);
    EXPECT_EQ(SPELL_POISON, m_player.affected->type);
    ASSERT_NE(nullptr, m_player.affected->next);
    EXPECT_EQ(SPELL_SANCTUARY, m_player.affected->next->type);
    ASSERT_NE(nullptr, m_player.affected->next->next);
    EXPECT_EQ(SPELL_ARMOR, m_player.affected->next->next->type);
    EXPECT_EQ(nullptr, m_player.affected->next->next->next);
}

TEST_F(AffectJoinTest, AddsTheAffectWhenThereIsNoneOfItsType)
{
    affected_type armor = make_affect(SPELL_ARMOR, OTHER_AFFECT_DURATION, OTHER_AFFECT_MODIFIER);
    affect_to_char(&m_player, &armor);
    affected_type poison = make_affect(SPELL_POISON, NEW_POISON_DURATION, NEW_POISON_MODIFIER);

    affect_join(&m_player, &poison, FALSE, FALSE);

    ASSERT_EQ(1, count_player_affects(SPELL_POISON));
    EXPECT_EQ(1, count_player_affects(SPELL_ARMOR));
    EXPECT_EQ(NEW_POISON_DURATION, affected_by_spell(&m_player, SPELL_POISON)->duration);
}

// Of two poisons already there, only the newer one, first in the list, takes the new poison in.
TEST_F(AffectJoinTest, MergesIntoTheFirstOfTwoAffectsOfTheType)
{
    affected_type older_poison
        = make_affect(SPELL_POISON, OLDER_POISON_DURATION, OLDER_POISON_MODIFIER);
    affect_to_char(&m_player, &older_poison);
    affected_type newer_poison
        = make_affect(SPELL_POISON, OLD_POISON_DURATION, OLD_POISON_MODIFIER);
    affect_to_char(&m_player, &newer_poison);
    affected_type joining_poison
        = make_affect(SPELL_POISON, NEW_POISON_DURATION, NEW_POISON_MODIFIER);

    affect_join(&m_player, &joining_poison, FALSE, FALSE);

    ASSERT_EQ(2, count_player_affects(SPELL_POISON));
    EXPECT_EQ(NEW_POISON_DURATION + OLD_POISON_DURATION, m_player.affected->duration);
    ASSERT_NE(nullptr, m_player.affected->next);
    EXPECT_EQ(OLDER_POISON_DURATION, m_player.affected->next->duration);
    EXPECT_EQ(OLDER_POISON_MODIFIER, m_player.affected->next->modifier);
}
