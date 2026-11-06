/* each.h

   Ranges over the singly linked lists in structs.h, for range-for and the standard algorithms
   that leave a sequence in place. Include this header to use them.

   A list is named by its link member, the pointer-to-member that leads from a node to the next.
   One struct can sit in several lists at once (char_data has next, next_in_room, next_fighting,
   ...), so the link chooses the list:

       for (char_data& character : intrusive::each<&char_data::next_in_room>(room.people)) {
           ...
       }

   The contracts live with the declarations:

       each.h                each() and each_removable(), and the walk rules
       list_range.h          the range both return, and the list_view names
       list_iterator.h       the iterator each() walks with
       removable_iterator.h  the iterator each_removable() walks with
       node_ref.h            what a mutable iterator yields, and the algorithms it admits
       link_traits.h         how a link's node type is found

   Nothing here owns or allocates, and nothing adds a field to a node. */

#ifndef INTRUSIVE_EACH_H
#define INTRUSIVE_EACH_H

#include "link_traits.h"
#include "list_iterator.h"
#include "list_range.h"
#include "removable_iterator.h"

#include <cstddef>

namespace intrusive {

/* The list starting at `head`, linked through `Link`; a null `head` is the empty list.

   The walk follows the live links: it reads the visited node's link after the body has run. The
   body may unlink or free other nodes, including the following one, provided the visited node is
   still linked into this list when the body ends. (The structs.h from-list functions null the
   link, so unlinking the visited node ends the walk early.) */
template <auto Link>
constexpr list_view<Link> each(detail::node_of<Link>* head) noexcept
{
    return list_view<Link>(head);
}

// As each() above, for a const head; the elements are const references.
template <auto Link>
constexpr const_list_view<Link> each(const detail::node_of<Link>* head) noexcept
{
    return const_list_view<Link>(head);
}

// The empty list, for a literal nullptr, which would otherwise match both overloads above.
template <auto Link>
constexpr list_view<Link> each(std::nullptr_t) noexcept
{
    return list_view<Link>(nullptr);
}

/* As each(), for a body that may unlink or free the node it is visiting. The walk reads the
   following node before the body runs, so the following node must survive the body and stay in
   the list. A node the body inserts directly after the visited one is not visited.

   Neither function handles a body that can remove both the visited and the following node, such
   as a kill that extracts other combatants, or extracting a container whose contents follow it in
   the same list. Such loops keep a cursor the removal repairs, restart the walk, or defer the
   removal. */
template <auto Link>
constexpr list_range<removable_iterator<Link>> each_removable(detail::node_of<Link>* head) noexcept
{
    return list_range<removable_iterator<Link>>(head);
}

} // namespace intrusive

#endif /* INTRUSIVE_EACH_H */
