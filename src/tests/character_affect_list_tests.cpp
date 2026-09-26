// character_affect_list on stack nodes: push_front() order and links, unlink() from every position
// and its removal count, the refusals that change nothing, a node far past MAX_AFFECT, contains()
// and its MAX_AFFECT bound, and the compile-time rules that keep the type trivial and its head
// unassignable.
#include "../character_affect_list.h"
#include "../handler.h"
#include "../spells.h"
#include "../structs.h"

#include <array>
#include <cstddef>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

static_assert(std::is_trivially_copyable_v<character_affect_list>,
              "char_data copies (a mob from its prototype) copy the list head bytewise");
static_assert(std::is_trivial_v<character_affect_list>,
              "clear_char()'s memset must leave an empty list with a zero count");
static_assert(!std::is_assignable_v<character_affect_list&, affected_type*>,
              "the head is written only through push_front() and unlink()");
static_assert(!std::is_constructible_v<character_affect_list, affected_type*>,
              "a list cannot be made from a bare node, bypassing push_front()");

namespace {

// More nodes than any test builds; a walk this long means the links have formed a cycle.
constexpr std::size_t node_walk_cap = 1000;

// The nodes from the head to the end of `list`, in list order. A walk past node_walk_cap fails
// the test and returns the nodes seen so far, so a cycle fails rather than hangs.
std::vector<const affected_type*> nodes_of(const character_affect_list& list) {
    std::vector<const affected_type*> nodes;
    for (const affected_type* node = list; node != nullptr; node = node->next) {
        if (nodes.size() >= node_walk_cap) {
            ADD_FAILURE() << "walked more than " << node_walk_cap << " nodes; the list has a cycle";
            break;
        }
        nodes.push_back(node);
    }
    return nodes;
}

// A list holding three stack nodes, built by pushing oldest first.
struct three_node_list {
    std::array<affected_type, 3> nodes{}; // the nodes; nodes[0] ends up at the head
    character_affect_list list{};         // the list under test, holding nodes[0], [1], [2]

    three_node_list() {
        list.push_front(&nodes[2]);
        list.push_front(&nodes[1]);
        list.push_front(&nodes[0]);
    }
};

// A character whose list holds `filler_count` nodes of type 0 ahead of one SPELL_POISON node.
template <std::size_t filler_count> struct poison_behind_fillers {
    affected_type poison{};                            // the wanted node, oldest on the list
    std::array<affected_type, filler_count> fillers{}; // pushed after the poison, so ahead of it
    char_data character{}; // holds the list, so affected_by_spell() can read it

    poison_behind_fillers() {
        poison.type = SPELL_POISON;
        character.affected.push_front(&poison);
        for (affected_type& filler : fillers) {
            character.affected.push_front(&filler);
        }
    }
};

} // namespace

TEST(CharacterAffectList, ZeroFilledListIsEmptyWithNoRemovals) {
    character_affect_list list{};

    EXPECT_EQ(static_cast<affected_type*>(list), nullptr);
    EXPECT_EQ(list.removal_count(), 0);
}

TEST(CharacterAffectList, PushFrontLinksNodesNewestFirst) {
    affected_type older{};
    affected_type newer{};
    character_affect_list list{};

    list.push_front(&older);
    list.push_front(&newer);

    EXPECT_EQ(static_cast<affected_type*>(list), &newer);
    EXPECT_EQ(list->next, &older) << "operator-> reads the head node";
    EXPECT_EQ(newer.next, &older);
    EXPECT_EQ(older.next, nullptr);
    EXPECT_EQ(list.removal_count(), 0) << "additions never count as removals";
}

TEST(CharacterAffectList, UnlinkingTheHeadRelinksTheRest) {
    three_node_list fixture;

    ASSERT_TRUE(fixture.list.unlink(&fixture.nodes[0]));

    const std::vector<const affected_type*> expected{&fixture.nodes[1], &fixture.nodes[2]};
    EXPECT_EQ(nodes_of(fixture.list), expected);
    EXPECT_EQ(fixture.list.removal_count(), 1);
}

TEST(CharacterAffectList, UnlinkingAMiddleNodeRelinksTheRest) {
    three_node_list fixture;

    ASSERT_TRUE(fixture.list.unlink(&fixture.nodes[1]));

    const std::vector<const affected_type*> expected{&fixture.nodes[0], &fixture.nodes[2]};
    EXPECT_EQ(nodes_of(fixture.list), expected);
    EXPECT_EQ(fixture.list.removal_count(), 1);
}

TEST(CharacterAffectList, UnlinkingTheTailRelinksTheRest) {
    three_node_list fixture;

    ASSERT_TRUE(fixture.list.unlink(&fixture.nodes[2]));

    const std::vector<const affected_type*> expected{&fixture.nodes[0], &fixture.nodes[1]};
    EXPECT_EQ(nodes_of(fixture.list), expected);
    EXPECT_EQ(fixture.list.removal_count(), 1);
}

TEST(CharacterAffectList, EachUnlinkRaisesTheCountByOne) {
    three_node_list fixture;

    ASSERT_TRUE(fixture.list.unlink(&fixture.nodes[1]));
    ASSERT_TRUE(fixture.list.unlink(&fixture.nodes[2]));
    ASSERT_TRUE(fixture.list.unlink(&fixture.nodes[0]));

    EXPECT_EQ(static_cast<affected_type*>(fixture.list), nullptr);
    EXPECT_EQ(fixture.list.removal_count(), 3);
}

TEST(CharacterAffectList, UnlinkingANodeNotOnTheListChangesNothing) {
    three_node_list fixture;
    affected_type stranger{};
    const std::vector<const affected_type*> before = nodes_of(fixture.list);

    EXPECT_FALSE(fixture.list.unlink(&stranger));

    EXPECT_EQ(nodes_of(fixture.list), before);
    EXPECT_EQ(fixture.list.removal_count(), 0);
}

TEST(CharacterAffectList, UnlinkingFromAnEmptyListChangesNothing) {
    affected_type stranger{};
    character_affect_list list{};

    EXPECT_FALSE(list.unlink(&stranger));

    EXPECT_EQ(static_cast<affected_type*>(list), nullptr);
    EXPECT_EQ(list.removal_count(), 0);
}

TEST(CharacterAffectList, NullNodesChangeNothing) {
    three_node_list fixture;
    const std::vector<const affected_type*> before = nodes_of(fixture.list);

    EXPECT_FALSE(fixture.list.unlink(nullptr));
    fixture.list.push_front(nullptr);

    EXPECT_EQ(nodes_of(fixture.list), before);
    EXPECT_EQ(fixture.list.removal_count(), 0);
}

TEST(CharacterAffectList, UnlinksANodeFarPastMaxAffect) {
    constexpr int filler_count = 2 * MAX_AFFECT;
    affected_type buried{};
    std::array<affected_type, filler_count> fillers{};
    character_affect_list list{};
    list.push_front(&buried);
    for (affected_type& filler : fillers) {
        list.push_front(&filler);
    }

    ASSERT_TRUE(list.unlink(&buried));

    const std::vector<const affected_type*> remaining = nodes_of(list);
    EXPECT_EQ(remaining.size(), fillers.size());
    EXPECT_EQ(fillers.front().next, nullptr) << "the oldest filler is now the tail";
    EXPECT_EQ(list.removal_count(), 1);
}

TEST(CharacterAffectList, EmptyListContainsNothing) {
    character_affect_list list{};

    EXPECT_FALSE(list.contains(0));
    EXPECT_FALSE(list.contains(SPELL_POISON));
}

TEST(CharacterAffectList, ContainsHeldTypesOnly) {
    three_node_list fixture;
    fixture.nodes[0].type = SPELL_ARMOR;
    fixture.nodes[1].type = SPELL_POISON;
    fixture.nodes[2].type = SPELL_HAZE;

    EXPECT_TRUE(fixture.list.contains(SPELL_POISON)) << "a type behind the head is found";
    EXPECT_TRUE(fixture.list.contains(SPELL_HAZE));
    EXPECT_FALSE(fixture.list.contains(SPELL_CURING));
}

TEST(CharacterAffectList, ContainsFindsANodeAtPositionMaxAffect) {
    poison_behind_fillers<MAX_AFFECT - 1> fixture;

    EXPECT_TRUE(fixture.character.affected.contains(SPELL_POISON));
    EXPECT_EQ(affected_by_spell(&fixture.character, SPELL_POISON), &fixture.poison);
}

TEST(CharacterAffectList, ContainsStopsAfterMaxAffectNodesLikeAffectedBySpell) {
    poison_behind_fillers<MAX_AFFECT> fixture;

    EXPECT_FALSE(fixture.character.affected.contains(SPELL_POISON));
    EXPECT_EQ(affected_by_spell(&fixture.character, SPELL_POISON), nullptr);
}

TEST(CharacterAffectList, ContainsWorksOnAConstList) {
    three_node_list fixture;
    fixture.nodes[2].type = SPELL_POISON;
    const character_affect_list& const_list = fixture.list;

    EXPECT_TRUE(const_list.contains(SPELL_POISON));
    EXPECT_FALSE(const_list.contains(SPELL_HAZE));
}
