/* intrusive_list.h

   STL-compatible iteration over the singly linked intrusive lists declared in structs.h.
   Requires C++17 (template <auto>); uses nothing newer.

   A list is named by its link member, because one struct can sit in several lists at once
   (char_data has next, next_in_room, next_fighting, next_fast_update and next_die):

       for (char_data& ch : intrusive::each<&char_data::next_in_room>(room.people)) { ... }

       auto people = intrusive::each<&char_data::next_in_room>(room.people);
       auto it = std::find_if(people.begin(), people.end(), [](const char_data& ch) { ... });

   Two ranges are provided:

     each<Link>(head)            LegacyForwardIterator. The list must not change while a loop or
                                 algorithm is running over it.
     each_removable<Link>(head)  LegacyInputIterator. The body may unlink or free the element it
                                 is visiting. Replaces the hand-written
                                 `next = node->link;` at the top of a loop body.

   Neither range owns anything, allocates, or adds a field to the node. An iterator is one
   pointer (two for each_removable). */

#ifndef INTRUSIVE_LIST_H
#define INTRUSIVE_LIST_H

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace intrusive {

namespace detail {

    /* Recovers Node from a link of type `Node* Node::*`. Any other template argument fails
       here, at the point of use, with "incomplete type link_traits<...>". */
    template <typename Link>
    struct link_traits;

    template <typename Node>
    struct link_traits<Node * Node::*> {
        using node_type = Node;
    };

    template <auto Link>
    using node_of = typename link_traits<decltype(Link)>::node_type;

} // namespace detail

/* ------------------------------------------------------------------------------------------
   Forward iterator. `Link` is the pointer-to-member that leads to the next node. It is part
   of the type, so iterators over different lists of the same struct cannot be mixed up:
   comparing a next_in_room iterator with a next_fighting iterator does not compile.
   ------------------------------------------------------------------------------------------ */
template <auto Link, bool IsConst = false>
class list_iterator {
    using node_type = detail::node_of<Link>;

public:
    // std::iterator is deprecated in C++17, so the five member types are declared directly.
    using iterator_category = std::forward_iterator_tag;
    using value_type = node_type;
    using difference_type = std::ptrdiff_t;
    using pointer = std::conditional_t<IsConst, const node_type*, node_type*>;
    using reference = std::conditional_t<IsConst, const node_type&, node_type&>;

    // A value-initialized iterator is the end of every list, since every list ends in null.
    constexpr list_iterator() noexcept = default;

    constexpr explicit list_iterator(pointer node) noexcept
        : node_(node)
    {
    }

    // iterator converts to const_iterator, never the other way.
    template <bool OtherIsConst,
        typename = std::enable_if_t<IsConst && !OtherIsConst>>
    constexpr list_iterator(const list_iterator<Link, OtherIsConst>& other) noexcept
        : node_(other.get())
    {
    }

    constexpr reference operator*() const noexcept { return *node_; }
    constexpr pointer operator->() const noexcept { return node_; }

    // The raw node pointer, for handing to the existing C-style functions. Null at the end.
    constexpr pointer get() const noexcept { return node_; }

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

    friend constexpr bool operator==(const list_iterator& a, const list_iterator& b) noexcept
    {
        return a.node_ == b.node_;
    }

    friend constexpr bool operator!=(const list_iterator& a, const list_iterator& b) noexcept
    {
        return a.node_ != b.node_;
    }

private:
    pointer node_ = nullptr;
};

/* ------------------------------------------------------------------------------------------
   Input iterator that reads the link before the caller sees the node, so the body may unlink
   or free the node it was given. It is tagged as an input iterator because once the body has
   changed the list, a copy of the iterator no longer walks the same sequence.

   It protects the current node only. If the body can also remove the *following* node (a kill
   that extracts the victim's followers, for instance), this is no safer than the saved-next
   loops it replaces.
   ------------------------------------------------------------------------------------------ */
template <auto Link>
class removable_iterator {
    using node_type = detail::node_of<Link>;

public:
    using iterator_category = std::input_iterator_tag;
    using value_type = node_type;
    using difference_type = std::ptrdiff_t;
    using pointer = node_type*;
    using reference = node_type&;

    constexpr removable_iterator() noexcept = default;

    constexpr explicit removable_iterator(pointer node) noexcept
        : node_(node)
        , next_(node ? node->*Link : nullptr)
    {
    }

    constexpr reference operator*() const noexcept { return *node_; }
    constexpr pointer operator->() const noexcept { return node_; }
    constexpr pointer get() const noexcept { return node_; }

    constexpr removable_iterator& operator++() noexcept
    {
        node_ = next_;
        next_ = node_ ? node_->*Link : nullptr;
        return *this;
    }

    constexpr removable_iterator operator++(int) noexcept
    {
        removable_iterator previous = *this;
        ++*this;
        return previous;
    }

    friend constexpr bool operator==(const removable_iterator& a,
        const removable_iterator& b) noexcept
    {
        return a.node_ == b.node_;
    }

    friend constexpr bool operator!=(const removable_iterator& a,
        const removable_iterator& b) noexcept
    {
        return a.node_ != b.node_;
    }

private:
    pointer node_ = nullptr;
    pointer next_ = nullptr;
};

/* ------------------------------------------------------------------------------------------
   A view of one list: a head pointer plus the iterator type. Cheap to copy, owns nothing.
   It holds the head by value, so build it where it is used rather than storing it: a range
   built before the head node is removed still starts at the removed node.
   ------------------------------------------------------------------------------------------ */
template <typename Iterator>
class list_range {
public:
    using iterator = Iterator;
    using const_iterator = Iterator;
    using value_type = typename Iterator::value_type;
    using reference = typename Iterator::reference;
    using difference_type = typename Iterator::difference_type;

    constexpr explicit list_range(typename Iterator::pointer head) noexcept
        : head_(head)
    {
    }

    constexpr Iterator begin() const noexcept { return Iterator(head_); }
    constexpr Iterator end() const noexcept { return Iterator(); }
    constexpr bool empty() const noexcept { return head_ == nullptr; }

    // The first node. The list must not be empty.
    constexpr reference front() const noexcept { return *head_; }

private:
    typename Iterator::pointer head_;
};

// The list starting at `head`, linked through `Link`. `head` may be null.
template <auto Link>
constexpr list_range<list_iterator<Link>> each(detail::node_of<Link>* head) noexcept
{
    return list_range<list_iterator<Link>>(head);
}

template <auto Link>
constexpr list_range<list_iterator<Link, true>> each(const detail::node_of<Link>* head) noexcept
{
    return list_range<list_iterator<Link, true>>(head);
}

// As each(), for loops whose body may unlink or free the node it is visiting.
template <auto Link>
constexpr list_range<removable_iterator<Link>> each_removable(detail::node_of<Link>* head) noexcept
{
    return list_range<removable_iterator<Link>>(head);
}

} // namespace intrusive

#endif /* INTRUSIVE_LIST_H */
