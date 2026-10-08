// Checks intrusive_list.h against the real structs.h from release-frodo.
// Build: g++ -std=c++17 -Wall -Wextra -pedantic -funsigned-char -isystem <repo>/src -I. intrusive_list_check.cpp

#include "structs.h"

#include "intrusive_list.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <numeric>
#include <type_traits>
#include <vector>

// The only out-of-line function char_data's destructor needs. Same body as char_utils.cpp, so
// this file links without the rest of the server.
void specialization_data::reset()
{
    delete current_spec_info;
    current_spec_info = NULL;
    current_spec = game_types::PS_None;
}

namespace {

// ---- Compile-time: iterator_traits and the forward-iterator surface -------------------------

using room_iter = intrusive::list_iterator<&char_data::next_in_room>;
using room_citer = intrusive::list_iterator<&char_data::next_in_room, true>;
using fight_iter = intrusive::list_iterator<&char_data::next_fighting>;
using safe_iter = intrusive::removable_iterator<&char_data::next_in_room>;

using room_traits = std::iterator_traits<room_iter>;
static_assert(std::is_same_v<room_traits::iterator_category, std::forward_iterator_tag>);
static_assert(std::is_same_v<room_traits::value_type, char_data>);
static_assert(std::is_same_v<room_traits::reference, char_data&>);
static_assert(std::is_same_v<room_traits::pointer, char_data*>);
static_assert(std::is_same_v<room_traits::difference_type, std::ptrdiff_t>);
static_assert(std::is_same_v<std::iterator_traits<room_citer>::reference, const char_data&>);
static_assert(std::is_same_v<std::iterator_traits<room_citer>::value_type, char_data>);
static_assert(std::is_same_v<std::iterator_traits<safe_iter>::iterator_category,
    std::input_iterator_tag>);

static_assert(std::is_default_constructible_v<room_iter>);
static_assert(std::is_trivially_copyable_v<room_iter>);
static_assert(std::is_nothrow_swappable_v<room_iter>);
static_assert(sizeof(room_iter) == sizeof(char_data*));
static_assert(std::is_same_v<decltype(*std::declval<room_iter>()), char_data&>);
static_assert(std::is_same_v<decltype(++std::declval<room_iter&>()), room_iter&>);
static_assert(std::is_same_v<decltype(std::declval<room_iter&>()++), room_iter>);

// iterator -> const_iterator only.
static_assert(std::is_convertible_v<room_iter, room_citer>);
static_assert(!std::is_convertible_v<room_citer, room_iter>);
static_assert(!std::is_constructible_v<room_iter, room_citer>);

// Different lists of the same struct are different, unrelated types.
static_assert(!std::is_same_v<room_iter, fight_iter>);
static_assert(!std::is_convertible_v<room_iter, fight_iter>);
static_assert(!std::is_constructible_v<fight_iter, room_iter>);

template <typename A, typename B, typename = void>
struct is_eq_comparable : std::false_type { };
template <typename A, typename B>
struct is_eq_comparable<A, B, std::void_t<decltype(std::declval<A>() == std::declval<B>())>>
    : std::true_type { };
static_assert(is_eq_comparable<room_iter, room_iter>::value);
static_assert(is_eq_comparable<room_iter, room_citer>::value);
static_assert(is_eq_comparable<room_citer, room_iter>::value);
static_assert(!is_eq_comparable<room_iter, fight_iter>::value);

// Constant evaluation works, on a toy node (char_data is not a literal type).
struct toy {
    int value;
    toy* next;
};
constexpr int sum_toys()
{
    toy c { 3, nullptr };
    toy b { 2, &c };
    toy a { 1, &b };
    int total = 0;
    for (const toy& t : intrusive::each<&toy::next>(&a))
        total += t.value;
    return total;
}
static_assert(sum_toys() == 6);

#if __cplusplus >= 202002L
// Extra evidence only; the header itself is C++17.
static_assert(std::forward_iterator<room_iter>);
static_assert(std::forward_iterator<room_citer>);
static_assert(std::input_iterator<safe_iter>);
static_assert(std::ranges::forward_range<intrusive::list_range<room_iter>>);
#endif

// ---- Runtime fixtures -----------------------------------------------------------------------

template <typename T>
T* zeroed()
{
    return new T();
}

struct room_fixture {
    char_data* people = nullptr;
    char_data* fighting = nullptr;
    std::vector<char_data*> all;

    explicit room_fixture(int count)
    {
        // Built back to front so the list reads 0, 1, 2, ... like char_to_room pushes do in
        // reverse. next_fighting is linked in the opposite order, over even levels only, to
        // prove the two lists really are walked independently.
        for (int level = count - 1; level >= 0; --level) {
            char_data* ch = zeroed<char_data>();
            ch->player.level = level;
            ch->next_in_room = people;
            people = ch;
            all.push_back(ch);
        }
        for (char_data* ch = people; ch; ch = ch->next_in_room) {
            if (ch->player.level % 2 == 0) {
                ch->next_fighting = fighting;
                fighting = ch;
            }
        }
    }

    ~room_fixture()
    {
        for (char_data* ch : all)
            delete ch;
    }
};

std::vector<int> levels_by_hand(const char_data* head, char_data* char_data::*link)
{
    std::vector<int> levels;
    for (const char_data* ch = head; ch; ch = ch->*link)
        levels.push_back(ch->player.level);
    return levels;
}

int failures = 0;
#define CHECK(expr)                                               \
    do {                                                          \
        if (!(expr)) {                                            \
            std::printf("FAILED line %d: %s\n", __LINE__, #expr); \
            ++failures;                                           \
        }                                                         \
    } while (0)

// ---- Tests ----------------------------------------------------------------------------------

void range_for_matches_hand_written_loop()
{
    room_fixture room(6);

    std::vector<int> seen;
    for (char_data& ch : intrusive::each<&char_data::next_in_room>(room.people))
        seen.push_back(ch.player.level);
    CHECK(seen == levels_by_hand(room.people, &char_data::next_in_room));
    CHECK((seen == std::vector<int> { 0, 1, 2, 3, 4, 5 }));

    seen.clear();
    for (char_data& ch : intrusive::each<&char_data::next_fighting>(room.fighting))
        seen.push_back(ch.player.level);
    CHECK(seen == levels_by_hand(room.fighting, &char_data::next_fighting));
    CHECK((seen == std::vector<int> { 4, 2, 0 }));
}

void empty_and_single_lists()
{
    char_data* none = nullptr;
    auto empty = intrusive::each<&char_data::next_in_room>(none);
    CHECK(empty.empty());
    CHECK(empty.begin() == empty.end());
    CHECK(std::distance(empty.begin(), empty.end()) == 0);
    int visits = 0;
    for (char_data& ch : empty) {
        (void)ch;
        ++visits;
    }
    CHECK(visits == 0);

    room_fixture room(1);
    auto one = intrusive::each<&char_data::next_in_room>(room.people);
    CHECK(!one.empty());
    CHECK(&one.front() == room.people);
    CHECK(std::distance(one.begin(), one.end()) == 1);
    CHECK(std::next(one.begin()) == one.end());
}

void standard_algorithms()
{
    room_fixture room(8);
    auto people = intrusive::each<&char_data::next_in_room>(room.people);

    CHECK(std::distance(people.begin(), people.end()) == 8);

    auto found = std::find_if(people.begin(), people.end(),
        [](const char_data& ch) { return ch.player.level == 5; });
    CHECK(found != people.end());
    CHECK(found->player.level == 5);
    CHECK(found.get() == room.all[8 - 1 - 5]);

    CHECK(std::count_if(people.begin(), people.end(),
              [](const char_data& ch) { return ch.player.level % 2 == 0; })
        == 4);
    CHECK(std::any_of(people.begin(), people.end(),
        [](const char_data& ch) { return ch.player.level == 7; }));
    CHECK(std::none_of(people.begin(), people.end(),
        [](const char_data& ch) { return ch.player.level > 7; }));

    // These need a forward iterator, not just an input iterator.
    auto highest = std::max_element(people.begin(), people.end(),
        [](const char_data& a, const char_data& b) { return a.player.level < b.player.level; });
    CHECK(highest->player.level == 7);
    CHECK(std::is_sorted(people.begin(), people.end(),
        [](const char_data& a, const char_data& b) { return a.player.level < b.player.level; }));
    auto pair = std::adjacent_find(people.begin(), people.end(),
        [](const char_data& a, const char_data& b) { return a.player.level + 1 != b.player.level; });
    CHECK(pair == people.end());
    auto bound = std::partition_point(people.begin(), people.end(),
        [](const char_data& ch) { return ch.player.level < 3; });
    CHECK(bound->player.level == 3);

    int total = std::accumulate(people.begin(), people.end(), 0,
        [](int sum, const char_data& ch) { return sum + ch.player.level; });
    CHECK(total == 28);

    // Collecting pointers, which is what most of the existing code wants to hold.
    std::vector<char_data*> pointers;
    std::transform(people.begin(), people.end(), std::back_inserter(pointers),
        [](char_data& ch) { return &ch; });
    CHECK(pointers.size() == 8);
    CHECK(pointers.front() == room.people);

    // Writing through the reference changes the node in place.
    std::for_each(people.begin(), people.end(), [](char_data& ch) { ch.player.level += 10; });
    CHECK(room.people->player.level == 10);
}

void multipass_guarantee()
{
    room_fixture room(5);
    auto people = intrusive::each<&char_data::next_in_room>(room.people);

    auto a = people.begin();
    auto b = a;
    CHECK(a == b);
    CHECK(&*a == &*b);
    ++a;
    CHECK(a != b);
    CHECK(b == people.begin()); // advancing a copy leaves the original alone
    ++b;
    CHECK(a == b);
    CHECK(&*a == &*b);

    auto before = a++;
    CHECK(before == b);
    CHECK(std::next(before) == a);

    intrusive::list_iterator<&char_data::next_in_room> value_initialized {};
    CHECK(value_initialized == people.end());

    // Two full passes see the same sequence.
    std::vector<const char_data*> first, second;
    for (char_data& ch : people)
        first.push_back(&ch);
    for (char_data& ch : people)
        second.push_back(&ch);
    CHECK(first == second);
}

void const_iteration()
{
    room_fixture room(4);
    const char_data* head = room.people;
    auto people = intrusive::each<&char_data::next_in_room>(head);
    static_assert(std::is_same_v<decltype(*people.begin()), const char_data&>);

    int total = 0;
    for (const char_data& ch : people)
        total += ch.player.level;
    CHECK(total == 6);

    auto mutable_begin = intrusive::each<&char_data::next_in_room>(room.people).begin();
    intrusive::list_iterator<&char_data::next_in_room, true> converted = mutable_begin;
    CHECK(converted == people.begin());
    CHECK(mutable_begin == people.begin());
    CHECK(people.begin() == mutable_begin);
    CHECK(mutable_begin != people.end());
}

void other_structs_from_structs_h()
{
    // obj_data is in two lists as well: next_content and next.
    obj_data* sword = zeroed<obj_data>();
    obj_data* shield = zeroed<obj_data>();
    obj_data* bag = zeroed<obj_data>();
    sword->item_number = 1;
    shield->item_number = 2;
    bag->item_number = 3;
    bag->contains = sword;
    sword->next_content = shield;
    sword->in_obj = shield->in_obj = bag;
    obj_data* object_list = bag;
    bag->next = shield;
    shield->next = sword;

    std::vector<int> inside, everything;
    for (obj_data& obj : intrusive::each<&obj_data::next_content>(bag->contains))
        inside.push_back(obj.item_number);
    for (obj_data& obj : intrusive::each<&obj_data::next>(object_list))
        everything.push_back(obj.item_number);
    CHECK((inside == std::vector<int> { 1, 2 }));
    CHECK((everything == std::vector<int> { 3, 2, 1 }));

    // affected_type, follow_type, extra_descr_data, descriptor_data all use a plain `next`.
    affected_type poison {}, bless {};
    poison.type = 33;
    bless.type = 3;
    poison.next = &bless;
    auto affects = intrusive::each<&affected_type::next>(&poison);
    CHECK(std::any_of(affects.begin(), affects.end(),
        [](const affected_type& aff) { return aff.type == 3; }));

    char_data* leader = zeroed<char_data>();
    char_data* pet = zeroed<char_data>();
    follow_type link { 7, pet, nullptr };
    leader->followers = &link;
    int followers = 0;
    for (follow_type& f : intrusive::each<&follow_type::next>(leader->followers)) {
        CHECK(f.follower == pet);
        ++followers;
    }
    CHECK(followers == 1);

    static_assert(std::is_same_v<
        std::iterator_traits<intrusive::list_iterator<&descriptor_data::next>>::value_type,
        descriptor_data>);
    static_assert(std::is_same_v<
        std::iterator_traits<intrusive::list_iterator<&memory_rec::next_on_mob>>::value_type,
        memory_rec>);
    static_assert(std::is_same_v<
        std::iterator_traits<intrusive::list_iterator<&universal_list::next>>::value_type,
        universal_list>);
    static_assert(std::is_same_v<
        std::iterator_traits<intrusive::list_iterator<&room_data::bfs_next>>::value_type,
        room_data>);

    delete sword;
    delete shield;
    delete bag;
    delete leader;
    delete pet;
}

// Same unlink logic as char_from_room in handler.cpp, without the world[] bookkeeping.
void unlink_from_room(char_data*& head, char_data* ch)
{
    if (ch == head) {
        head = ch->next_in_room;
    } else {
        char_data* prev = head;
        while (prev && prev->next_in_room != ch)
            prev = prev->next_in_room;
        if (!prev)
            return;
        prev->next_in_room = ch->next_in_room;
    }
    // Poison the link the way a freed or re-homed character would: following it afterwards
    // must not happen.
    ch->next_in_room = reinterpret_cast<char_data*>(static_cast<std::uintptr_t>(0xdead));
}

void removable_range_survives_removing_the_current_node()
{
    room_fixture room(7);
    char_data* head = room.people;

    std::vector<int> visited;
    for (char_data& ch : intrusive::each_removable<&char_data::next_in_room>(head)) {
        visited.push_back(ch.player.level);
        if (ch.player.level % 2 == 0) // removes the head, interior nodes, and the tail
            unlink_from_room(head, &ch);
    }
    CHECK((visited == std::vector<int> { 0, 1, 2, 3, 4, 5, 6 }));
    CHECK((levels_by_hand(head, &char_data::next_in_room) == std::vector<int> { 1, 3, 5 }));

    // Removing every node empties the list.
    for (char_data& ch : intrusive::each_removable<&char_data::next_in_room>(head))
        unlink_from_room(head, &ch);
    CHECK(head == nullptr);

    char_data* none = nullptr;
    auto empty = intrusive::each_removable<&char_data::next_in_room>(none);
    CHECK(empty.begin() == empty.end());
}

} // namespace

int main()
{
    range_for_matches_hand_written_loop();
    empty_and_single_lists();
    standard_algorithms();
    multipass_guarantee();
    const_iteration();
    other_structs_from_structs_h();
    removable_range_survives_removing_the_current_node();

    if (failures == 0)
        std::printf("all checks passed\n");
    return failures == 0 ? 0 : 1;
}
