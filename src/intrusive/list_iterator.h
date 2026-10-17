/* list_iterator.h

   Part of the intrusive list ranges; include each.h to use them. */

#ifndef INTRUSIVE_LIST_ITERATOR_H
#define INTRUSIVE_LIST_ITERATOR_H

#include "link_traits.h"
#include "node_ref.h"

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace intrusive {

/* Forward iterator over the list linked through `Link`, the pointer-to-member that leads to the
   next node. The link is part of the type, so iterators over different lists of the same struct
   cannot be mixed up: comparing a next_in_room iterator with a next_fighting iterator does not
   compile.

   With IsConst set it yields `const Node&`. Otherwise it yields node_ref<Node>, which keeps
   rearranging algorithms and `auto&` bindings from compiling (see node_ref). The category stays
   forward although C++17 asks a forward iterator for a true reference, following
   std::vector<bool>::iterator; libstdc++ and libc++ algorithms accept it. */
template <auto Link, bool IsConst = false>
class list_iterator {
    // The struct the list links together.
    using node_type = detail::node_of<Link>;

public:
    // The standard iterator member types; std::iterator is deprecated in C++17.
    using iterator_category = std::forward_iterator_tag;
    using value_type = node_type;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<IsConst, const node_type*, node_type*>;
    using reference = std::conditional_t<IsConst, const node_type&, node_ref<node_type>>;

    // A value-initialized iterator is the end of every list, since every list ends in null.
    constexpr list_iterator() noexcept = default;

    // Starts at `node`; a null `node` constructs the end iterator.
    constexpr explicit list_iterator(pointer node) noexcept
        : node_(node)
    {
    }

    // A mutable iterator converts to a const iterator, never the other way.
    template <bool OtherIsConst, typename = std::enable_if_t<IsConst && !OtherIsConst>>
    constexpr list_iterator(const list_iterator<Link, OtherIsConst>& other) noexcept
        : node_(other.get())
    {
    }

    /* The standard forward-iterator operations. operator-> gives the raw node pointer. Two
       iterators are equal when they are at the same node, and a mutable iterator compares with a
       const one. Dereferencing or incrementing the end iterator is undefined. */
    constexpr reference operator*() const noexcept
    {
        if constexpr (IsConst) {
            return *node_;
        } else {
            return reference(node_);
        }
    }

    constexpr pointer operator->() const noexcept { return node_; }

    constexpr list_iterator& operator++() noexcept
    {
        node_ = node_->*Link;
        return *this;
    }

    constexpr list_iterator operator++(int) noexcept
    {
        list_iterator previous = *this;
        ++*this;
        return previous;
    }

    friend constexpr bool operator==(const list_iterator& left, const list_iterator& right) noexcept
    {
        return left.node_ == right.node_;
    }

    friend constexpr bool operator!=(const list_iterator& left, const list_iterator& right) noexcept
    {
        return left.node_ != right.node_;
    }

    // The raw node pointer, for handing to the existing C-style functions. Null at the end.
    constexpr pointer get() const noexcept { return node_; }

private:
    // The node the iterator is at; null at the end.
    pointer node_ = nullptr;
};

} // namespace intrusive

#endif /* INTRUSIVE_LIST_ITERATOR_H */
