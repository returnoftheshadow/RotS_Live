/* list_range.h

   Part of the intrusive list ranges; include each.h to use them. */

#ifndef INTRUSIVE_LIST_RANGE_H
#define INTRUSIVE_LIST_RANGE_H

#include "list_iterator.h"

#include <type_traits>

namespace intrusive {

/* One list as a range for range-for and the standard algorithms, walked with `Iterator`. It owns
   nothing and is cheap to copy. It holds the head by value, so build it where it is used rather
   than storing it: a range built before the head node is removed still starts at the removed
   node. */
template <typename Iterator>
class list_range {
public:
    // The iterator begin() and end() return, and its element types.
    using iterator = Iterator;
    using value_type = typename Iterator::value_type;
    using reference = typename Iterator::reference;
    using pointer = typename Iterator::pointer;
    using difference_type = typename Iterator::difference_type;

    // The list starting at `head`; a null `head` is the empty list.
    constexpr explicit list_range(pointer head) noexcept
        : head_(head)
    {
    }

    // The first node, or end() when the list is empty.
    constexpr Iterator begin() const noexcept { return Iterator(head_); }

    // The end of the list.
    constexpr Iterator end() const noexcept { return Iterator(); }

    // Whether the list has no nodes.
    constexpr bool empty() const noexcept { return head_ == nullptr; }

    // The first node, as a plain reference so `front().field` works. The list must not be empty.
    constexpr std::remove_pointer_t<pointer>& front() const noexcept { return *head_; }

private:
    // The first node; null for the empty list.
    pointer head_;
};

// The range each() returns for the list linked through `Link`.
template <auto Link>
using list_view = list_range<list_iterator<Link>>;

// As list_view, walked with the const iterator.
template <auto Link>
using const_list_view = list_range<list_iterator<Link, true>>;

} // namespace intrusive

#endif /* INTRUSIVE_LIST_RANGE_H */
