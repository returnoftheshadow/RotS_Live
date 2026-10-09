/* link_traits.h

   Part of the intrusive list ranges; include each.h to use them. */

#ifndef INTRUSIVE_LINK_TRAITS_H
#define INTRUSIVE_LINK_TRAITS_H

namespace intrusive {

namespace detail {

    /* Recovers Node from a link of type `Node* Node::*`. Any other template argument fails
       here, at the point of use, with "incomplete type link_traits<...>". */
    template <typename Link>
    struct link_traits;

    // The one valid form: a member of Node that points to the next Node.
    template <typename Node>
    struct link_traits<Node * Node::*> {
        // The struct the list links together.
        using node_type = Node;
    };

    // The node type of the list linked through `Link`.
    template <auto Link>
    using node_of = typename link_traits<decltype(Link)>::node_type;

} // namespace detail

} // namespace intrusive

#endif /* INTRUSIVE_LINK_TRAITS_H */
