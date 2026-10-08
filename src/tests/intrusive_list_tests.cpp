#include "../intrusive_list.h"
#include "../room_lists.h"
#include "../structs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <memory>
#include <numeric>
#include <type_traits>
#include <vector>

namespace {

// ---- Compile-time: iterator_traits and the forward-iterator surface ------------------------

using room_iterator = intrusive::list_iterator<&char_data::next_in_room>;
using room_const_iterator = intrusive::list_iterator<&char_data::next_in_room, true>;
using fighting_iterator = intrusive::list_iterator<&char_data::next_fighting>;
using room_removable_iterator = intrusive::removable_iterator<&char_data::next_in_room>;

using room_traits = std::iterator_traits<room_iterator>;
static_assert(std::is_same_v<room_traits::iterator_category, std::forward_iterator_tag>);
static_assert(std::is_same_v<room_traits::value_type, char_data>);
static_assert(std::is_same_v<room_traits::reference, char_data&>);
static_assert(std::is_same_v<room_traits::pointer, char_data*>);
static_assert(std::is_same_v<room_traits::difference_type, std::ptrdiff_t>);
static_assert(std::is_same_v<std::iterator_traits<room_const_iterator>::reference, const char_data&>);
static_assert(std::is_same_v<std::iterator_traits<room_const_iterator>::value_type, char_data>);
static_assert(std::is_same_v<std::iterator_traits<room_removable_iterator>::iterator_category,
    std::input_iterator_tag>);

static_assert(std::is_default_constructible_v<room_iterator>);
static_assert(std::is_trivially_copyable_v<room_iterator>);
static_assert(std::is_nothrow_swappable_v<room_iterator>);
static_assert(sizeof(room_iterator) == sizeof(char_data*));
static_assert(std::is_same_v<decltype(*std::declval<room_iterator>()), char_data&>);
static_assert(std::is_same_v<decltype(++std::declval<room_iterator&>()), room_iterator&>);
static_assert(std::is_same_v<decltype(std::declval<room_iterator&>()++), room_iterator>);

// An iterator converts to a const iterator, never the other way.
static_assert(std::is_convertible_v<room_iterator, room_const_iterator>);
static_assert(!std::is_convertible_v<room_const_iterator, room_iterator>);
static_assert(!std::is_constructible_v<room_iterator, room_const_iterator>);

// Different lists of the same struct are different, unrelated types.
static_assert(!std::is_same_v<room_iterator, fighting_iterator>);
static_assert(!std::is_convertible_v<room_iterator, fighting_iterator>);
static_assert(!std::is_constructible_v<fighting_iterator, room_iterator>);

template <typename Left, typename Right, typename = void>
struct is_equality_comparable : std::false_type { };
template <typename Left, typename Right>
struct is_equality_comparable<Left, Right,
    std::void_t<decltype(std::declval<Left>() == std::declval<Right>())>> : std::true_type { };
static_assert(is_equality_comparable<room_iterator, room_iterator>::value);
static_assert(is_equality_comparable<room_iterator, room_const_iterator>::value);
static_assert(is_equality_comparable<room_const_iterator, room_iterator>::value);
static_assert(!is_equality_comparable<room_iterator, fighting_iterator>::value);

// Iteration works in a constant expression, on a literal node type (char_data is not one).
struct literal_node {
    // The payload summed by sum_literal_nodes.
    int value;
    // The following node, or null at the end.
    literal_node* next;
};

constexpr int sum_literal_nodes()
{
    literal_node third { 3, nullptr };
    literal_node second { 2, &third };
    literal_node first { 1, &second };
    int total = 0;
    for (const literal_node& node : intrusive::each<&literal_node::next>(&first)) {
        total += node.value;
    }
    return total;
}
static_assert(sum_literal_nodes() == 6);

// ---- Runtime fixtures ---------------------------------------------------------------------

// Characters linked into a room list reading levels 0, 1, 2, ..., and a fighting list over the
// even levels in the opposite order, so a test can tell the two lists apart.
class CharacterChain {
public:
    explicit CharacterChain(int count)
    {
        for (int level = count - 1; level >= 0; --level) {
            m_characters.push_back(std::make_unique<char_data>());
            char_data* character = m_characters.back().get();
            character->player.level = level;
            character->next_in_room = m_people;
            m_people = character;
        }
        for (char_data* character = m_people; character != nullptr;
            character = character->next_in_room) {
            if (character->player.level % 2 == 0) {
                character->next_fighting = m_fighting;
                m_fighting = character;
            }
        }
    }

    // Head of the room list, level 0 first.
    char_data* people() const { return m_people; }

    // Head of the fighting list, highest even level first.
    char_data* fighting() const { return m_fighting; }

    // The character at `level`, which the constructor gave a unique level.
    char_data* at_level(int level) const
    {
        char_data* character = m_people;
        while (character != nullptr && character->player.level != level) {
            character = character->next_in_room;
        }
        return character;
    }

private:
    // Owns every character; destroyed with the chain.
    std::vector<std::unique_ptr<char_data>> m_characters;
    // First character in the room list.
    char_data* m_people = nullptr;
    // First character in the fighting list.
    char_data* m_fighting = nullptr;
};

std::vector<int> levels_by_hand(const char_data* head, char_data* char_data::* link)
{
    std::vector<int> levels;
    for (const char_data* character = head; character != nullptr; character = character->*link) {
        levels.push_back(character->player.level);
    }
    return levels;
}

// Unlinks `character` from the room list starting at `head`, as char_from_room does, then
// poisons its link so a walk that follows it afterwards fails loudly.
void unlink_from_room(char_data*& head, char_data* character)
{
    if (character == head) {
        head = character->next_in_room;
    } else {
        char_data* previous = head;
        while (previous != nullptr && previous->next_in_room != character) {
            previous = previous->next_in_room;
        }
        if (previous == nullptr) {
            ADD_FAILURE() << "character at level " << character->player.level << " is not in the room";
            return;
        }
        previous->next_in_room = character->next_in_room;
    }
    character->next_in_room = reinterpret_cast<char_data*>(static_cast<std::uintptr_t>(0xdead));
}

// ---- Tests ---------------------------------------------------------------------------------

TEST(IntrusiveList, RangeForVisitsTheSameNodesAsAHandWrittenLoop)
{
    CharacterChain chain(6);

    std::vector<int> seen;
    for (char_data& character : intrusive::each<&char_data::next_in_room>(chain.people())) {
        seen.push_back(character.player.level);
    }
    EXPECT_EQ(levels_by_hand(chain.people(), &char_data::next_in_room), seen);
    EXPECT_EQ((std::vector<int> { 0, 1, 2, 3, 4, 5 }), seen);

    seen.clear();
    for (char_data& character : intrusive::each<&char_data::next_fighting>(chain.fighting())) {
        seen.push_back(character.player.level);
    }
    EXPECT_EQ(levels_by_hand(chain.fighting(), &char_data::next_fighting), seen);
    EXPECT_EQ((std::vector<int> { 4, 2, 0 }), seen) << "the fighting list is walked through next_fighting";
}

TEST(IntrusiveList, AnEmptyListHasNoNodes)
{
    char_data* no_one = nullptr;
    auto empty = intrusive::each<&char_data::next_in_room>(no_one);

    EXPECT_TRUE(empty.empty());
    EXPECT_TRUE(empty.begin() == empty.end());
    EXPECT_EQ(0, std::distance(empty.begin(), empty.end()));
}

TEST(IntrusiveList, ASingleNodeListHasOneNode)
{
    CharacterChain chain(1);
    auto one = intrusive::each<&char_data::next_in_room>(chain.people());

    EXPECT_FALSE(one.empty());
    EXPECT_EQ(chain.people(), &one.front());
    EXPECT_EQ(1, std::distance(one.begin(), one.end()));
    EXPECT_TRUE(std::next(one.begin()) == one.end());
}

TEST(IntrusiveList, WorksWithStandardAlgorithms)
{
    CharacterChain chain(8);
    auto people = intrusive::each<&char_data::next_in_room>(chain.people());

    EXPECT_EQ(8, std::distance(people.begin(), people.end()));

    auto found = std::find_if(people.begin(), people.end(),
        [](const char_data& character) -> bool { return character.player.level == 5; });
    ASSERT_TRUE(found != people.end());
    EXPECT_EQ(chain.at_level(5), found.get());

    EXPECT_EQ(4, std::count_if(people.begin(), people.end(), [](const char_data& character) -> bool {
        return character.player.level % 2 == 0;
    }));
    EXPECT_TRUE(std::none_of(people.begin(), people.end(),
        [](const char_data& character) -> bool { return character.player.level > 7; }));

    // These need a forward iterator, not just an input iterator.
    auto by_level = [](const char_data& left, const char_data& right) -> bool {
        return left.player.level < right.player.level;
    };
    EXPECT_EQ(7, std::max_element(people.begin(), people.end(), by_level)->player.level);
    EXPECT_TRUE(std::is_sorted(people.begin(), people.end(), by_level));
    auto below_three = std::partition_point(people.begin(), people.end(),
        [](const char_data& character) -> bool { return character.player.level < 3; });
    EXPECT_EQ(3, below_three->player.level);

    int total = std::accumulate(people.begin(), people.end(), 0,
        [](int sum, const char_data& character) -> int { return sum + character.player.level; });
    EXPECT_EQ(28, total);

    std::for_each(people.begin(), people.end(),
        [](char_data& character) -> void { character.player.level += 10; });
    EXPECT_EQ(10, chain.people()->player.level) << "writing through the reference changes the node";
}

TEST(IntrusiveList, CopiesOfAnIteratorAdvanceIndependently)
{
    CharacterChain chain(5);
    auto people = intrusive::each<&char_data::next_in_room>(chain.people());

    auto leader = people.begin();
    auto follower = leader;
    ++leader;
    EXPECT_TRUE(follower == people.begin()) << "advancing a copy leaves the original in place";
    ++follower;
    EXPECT_TRUE(leader == follower);
    EXPECT_EQ(&*leader, &*follower);

    auto before = leader++;
    EXPECT_TRUE(before == follower);
    EXPECT_TRUE(std::next(before) == leader);

    room_iterator value_initialized { };
    EXPECT_TRUE(value_initialized == people.end()) << "a value-initialized iterator is the end";

    std::vector<const char_data*> first_pass;
    std::vector<const char_data*> second_pass;
    for (char_data& character : people) {
        first_pass.push_back(&character);
    }
    for (char_data& character : people) {
        second_pass.push_back(&character);
    }
    EXPECT_EQ(first_pass, second_pass);
}

TEST(IntrusiveList, AConstHeadYieldsConstReferences)
{
    CharacterChain chain(4);
    const char_data* head = chain.people();
    auto people = intrusive::each<&char_data::next_in_room>(head);
    static_assert(std::is_same_v<decltype(*people.begin()), const char_data&>);

    int total = 0;
    for (const char_data& character : people) {
        total += character.player.level;
    }
    EXPECT_EQ(6, total);

    room_iterator mutable_begin = intrusive::each<&char_data::next_in_room>(chain.people()).begin();
    room_const_iterator converted = mutable_begin;
    EXPECT_TRUE(converted == people.begin());
    EXPECT_TRUE(mutable_begin == people.begin());
    EXPECT_TRUE(people.begin() == mutable_begin);
}

TEST(IntrusiveList, WalksObjectContentsAndTheObjectListSeparately)
{
    obj_data sword { };
    obj_data shield { };
    obj_data bag { };
    sword.item_number = 1;
    shield.item_number = 2;
    bag.item_number = 3;
    bag.contains = &sword;
    sword.next_content = &shield;
    bag.next = &shield;
    shield.next = &sword;

    std::vector<int> inside_bag;
    std::vector<int> everything;
    for (obj_data& item : intrusive::each<&obj_data::next_content>(bag.contains)) {
        inside_bag.push_back(item.item_number);
    }
    for (obj_data& item : intrusive::each<&obj_data::next>(&bag)) {
        everything.push_back(item.item_number);
    }
    EXPECT_EQ((std::vector<int> { 1, 2 }), inside_bag);
    EXPECT_EQ((std::vector<int> { 3, 2, 1 }), everything);
}

TEST(IntrusiveList, WalksFollowersThroughTheirOwnLink)
{
    char_data leader { };
    char_data pet { };
    follow_type pet_link { };
    pet_link.follower = &pet;
    leader.followers = &pet_link;

    int follower_count = 0;
    for (follow_type& link : intrusive::each<&follow_type::next>(leader.followers)) {
        EXPECT_EQ(&pet, link.follower);
        ++follower_count;
    }
    EXPECT_EQ(1, follower_count);
}

TEST(IntrusiveList, RemovableRangeSurvivesUnlinkingTheVisitedNode)
{
    CharacterChain chain(7);
    char_data* head = chain.people();

    std::vector<int> visited;
    for (char_data& character : intrusive::each_removable<&char_data::next_in_room>(head)) {
        visited.push_back(character.player.level);
        if (character.player.level % 2 == 0) {
            unlink_from_room(head, &character); // the head, interior nodes and the tail
        }
    }
    EXPECT_EQ((std::vector<int> { 0, 1, 2, 3, 4, 5, 6 }), visited);
    EXPECT_EQ((std::vector<int> { 1, 3, 5 }), levels_by_hand(head, &char_data::next_in_room));

    for (char_data& character : intrusive::each_removable<&char_data::next_in_room>(head)) {
        unlink_from_room(head, &character);
    }
    EXPECT_EQ(nullptr, head) << "unlinking every node empties the list";
}

TEST(RoomLists, PeopleInWalksTheRoomsCharacters)
{
    CharacterChain chain(3);
    room_data room;
    room.people = chain.people();
    room.contents = nullptr;

    const room_people_range people = people_in(room);
    std::vector<int> levels;
    for (const char_data& character : people) {
        levels.push_back(character.player.level);
    }
    EXPECT_EQ((std::vector<int> { 0, 1, 2 }), levels);
}

TEST(RoomLists, ContentsOfWalksTheFloorButNotInsideContainers)
{
    obj_data torch { };
    obj_data bag { };
    obj_data coin { };
    torch.item_number = 1;
    bag.item_number = 2;
    coin.item_number = 3;
    torch.next_content = &bag;
    bag.contains = &coin;
    room_data room;
    room.people = nullptr;
    room.contents = &torch;

    const room_contents_range contents = contents_of(room);
    std::vector<int> item_numbers;
    std::transform(std::begin(contents), std::end(contents), std::back_inserter(item_numbers),
        [](const obj_data& item) -> int { return item.item_number; });
    EXPECT_EQ((std::vector<int> { 1, 2 }), item_numbers);
}

} // namespace
