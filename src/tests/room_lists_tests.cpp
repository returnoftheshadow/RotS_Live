#include "../room_lists.h"
#include "../structs.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <iterator>
#include <type_traits>
#include <vector>

namespace {

// A const room still yields mutable people and objects: the room holds its lists as pointers.
using const_room_people = decltype(people_in(std::declval<const room_data&>()));
using const_room_contents = decltype(contents_of(std::declval<const room_data&>()));
static_assert(std::is_same_v<const_room_people::reference, intrusive::node_ref<char_data>>);
static_assert(std::is_same_v<const_room_contents::reference, intrusive::node_ref<obj_data>>);

TEST(RoomLists, PeopleInWalksTheRoomsCharacters)
{
    char_data first { };
    char_data second { };
    char_data third { };
    first.player.level = 0;
    second.player.level = 1;
    third.player.level = 2;
    first.next_in_room = &second;
    second.next_in_room = &third;
    room_data room;
    room.people = &first;
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
