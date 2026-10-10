/* removable_iterator.h

   Part of the intrusive list ranges; include each.h to use them. */

#ifndef INTRUSIVE_REMOVABLE_ITERATOR_H
#define INTRUSIVE_REMOVABLE_ITERATOR_H

#include "link_traits.h"
#include "node_ref.h"

#include <cstddef>
#include <iterator>

namespace intrusive {

/* Input iterator over the list linked through `Link` that reads each node's link on arriving at
   it, so the node it is at may be unlinked or freed before it advances (each_removable states the
   loop contract). Once the list changes, copies no longer walk the same sequence, so the category
   is input. It yields node_ref<Node>, as the mutable list_iterator does. */
template <auto Link>
class removable_iterator {
    // The struct the list links together.
    using node_type = detail::node_of<Link>;

public:
    // The standard iterator member types; std::iterator is deprecated in C++17.
    using iterator_category = std::input_iterator_tag;
    using value_type = node_type;
    using difference_type = std::ptrdiff_t;
    using pointer = node_type*;
    using reference = node_ref<node_type>;

    // A value-initialized iterator is the end of every list.
    constexpr removable_iterator() noexcept = default;

    // Starts at `node` and reads its link; a null `node` constructs the end iterator.
    constexpr explicit removable_iterator(pointer node) noexcept
        : node_(node)
        , next_(link_of(node))
    {
    }

    /* The standard input-iterator operations. operator-> gives the raw node pointer. Advancing
       moves to the node read on arrival and reads that node's link. Two iterators are equal when
       they are at the same node. Dereferencing or incrementing the end iterator is undefined. */
    constexpr reference operator*() const noexcept { return reference(node_); }
    constexpr pointer operator->() const noexcept { return node_; }

    constexpr removable_iterator& operator++() noexcept
    {
        node_ = next_;
        next_ = link_of(node_);
        return *this;
    }

    constexpr removable_iterator operator++(int) noexcept
    {
        removable_iterator previous = *this;
        ++*this;
        return previous;
    }

    friend constexpr bool operator==(const removable_iterator& left,
        const removable_iterator& right) noexcept
    {
        return left.node_ == right.node_;
    }

    friend constexpr bool operator!=(const removable_iterator& left,
        const removable_iterator& right) noexcept
    {
        return left.node_ != right.node_;
    }

    // The raw node pointer, for handing to the existing C-style functions. Null at the end.
    constexpr pointer get() const noexcept { return node_; }

private:
    // The node after `node`, or null when `node` is null.
    static constexpr pointer link_of(pointer node) noexcept
    {
        if (node == nullptr) {
            return nullptr;
        }
        return node->*Link;
    }

    // The node the iterator is at; null at the end.
    pointer node_ = nullptr;
    // The node after node_, read on arriving at node_; null when node_ is the last or the end.
    pointer next_ = nullptr;
};

} // namespace intrusive

#endif /* INTRUSIVE_REMOVABLE_ITERATOR_H */
