/* affect_join, which merges a new affect into one of the same type the character already has
 * (a poison landing on someone already poisoned) or else adds it. */
#include "scripted_room_fixture.h"

#include "../spells.h"

using namespace scripted_room;

namespace {

/* The duration and modifier of the poison the player already has. */
constexpr int OLD_POISON_DURATION = 10;
constexpr int OLD_POISON_MODIFIER = 4;

class AffectJoinTest : public ScriptedRoomFixture {
protected:
    void TearDown() override
    {
        while (m_player.affected) {
            affect_remove(&m_player, m_player.affected);
        }
        ScriptedRoomFixture::TearDown();
    }

    /* An affect of <type> for <duration> hours with <modifier>, changing nothing. */
    static affected_type make_affect(int type, int duration, int modifier)
    {
        affected_type affect {};
        affect.type = type;
        affect.duration = duration;
        affect.modifier = modifier;
        affect.location = APPLY_NONE;
        return affect;
    }

    /* How many affects of <type> the player has. */
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
    affected_type new_poison = make_affect(SPELL_POISON, 3, 1);

    affect_join(&m_player, &new_poison, FALSE, FALSE);

    ASSERT_EQ(1, count_player_affects(SPELL_POISON));
    const affected_type* merged = affected_by_spell(&m_player, SPELL_POISON);
    EXPECT_EQ(3 + OLD_POISON_DURATION, merged->duration);
    EXPECT_EQ(1 + OLD_POISON_MODIFIER, merged->modifier);
}

/* A longer or stronger new poison keeps its own duration or modifier instead of adding the old. */
TEST_F(AffectJoinTest, AStrongerNewPoisonKeepsItsOwnValues)
{
    affected_type old_poison = make_affect(SPELL_POISON, OLD_POISON_DURATION, OLD_POISON_MODIFIER);
    affect_to_char(&m_player, &old_poison);
    affected_type new_poison = make_affect(SPELL_POISON, OLD_POISON_DURATION + 5, OLD_POISON_MODIFIER + 2);

    affect_join(&m_player, &new_poison, FALSE, FALSE);

    ASSERT_EQ(1, count_player_affects(SPELL_POISON));
    const affected_type* merged = affected_by_spell(&m_player, SPELL_POISON);
    EXPECT_EQ(OLD_POISON_DURATION + 5, merged->duration);
    EXPECT_EQ(OLD_POISON_MODIFIER + 2, merged->modifier);
}

/* The merged affect is the newest, at the head of the list; the others keep their order. */
TEST_F(AffectJoinTest, PutsTheMergedAffectFirstAndKeepsTheOthers)
{
    affected_type armor = make_affect(SPELL_ARMOR, 5, 0);
    affect_to_char(&m_player, &armor);
    affected_type old_poison = make_affect(SPELL_POISON, OLD_POISON_DURATION, OLD_POISON_MODIFIER);
    affect_to_char(&m_player, &old_poison);
    affected_type sanctuary = make_affect(SPELL_SANCTUARY, 5, 0);
    affect_to_char(&m_player, &sanctuary);
    affected_type new_poison = make_affect(SPELL_POISON, 3, 1);

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
    affected_type armor = make_affect(SPELL_ARMOR, 5, 0);
    affect_to_char(&m_player, &armor);
    affected_type poison = make_affect(SPELL_POISON, 3, 1);

    affect_join(&m_player, &poison, FALSE, FALSE);

    ASSERT_EQ(1, count_player_affects(SPELL_POISON));
    EXPECT_EQ(1, count_player_affects(SPELL_ARMOR));
    EXPECT_EQ(3, affected_by_spell(&m_player, SPELL_POISON)->duration);
}

/* Of two poisons already there, only the newer one, first in the list, takes the new poison in. */
TEST_F(AffectJoinTest, MergesIntoTheFirstOfTwoAffectsOfTheType)
{
    affected_type older_poison = make_affect(SPELL_POISON, 7, 2);
    affect_to_char(&m_player, &older_poison);
    affected_type newer_poison = make_affect(SPELL_POISON, OLD_POISON_DURATION, OLD_POISON_MODIFIER);
    affect_to_char(&m_player, &newer_poison);
    affected_type joining_poison = make_affect(SPELL_POISON, 3, 1);

    affect_join(&m_player, &joining_poison, FALSE, FALSE);

    ASSERT_EQ(2, count_player_affects(SPELL_POISON));
    EXPECT_EQ(3 + OLD_POISON_DURATION, m_player.affected->duration);
    ASSERT_NE(nullptr, m_player.affected->next);
    EXPECT_EQ(7, m_player.affected->next->duration);
    EXPECT_EQ(2, m_player.affected->next->modifier);
}
