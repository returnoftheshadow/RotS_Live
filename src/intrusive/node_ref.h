/* node_ref.h

   Part of the intrusive list ranges; include each.h to use them. */

#ifndef INTRUSIVE_NODE_REF_H
#define INTRUSIVE_NODE_REF_H

namespace intrusive {

/* Refers to one node of a list. It converts to `Node&`, so code that names the node type reads
   as it would with a plain reference: range-for with an explicit type
   (`for (char_data& character : ...)`), callbacks taking `char_data&` or `const char_data&`,
   and the algorithms that leave the sequence in place (find_if, count_if, all_of, accumulate,
   max_element, adjacent_find, distance, for_each, transform into a container of pointers).

   It cannot be assigned or swapped. Algorithms that rearrange a sequence by assigning or swapping
   whole nodes, which would copy links along with payloads, therefore do not compile: remove_if,
   unique, partition, rotate, reverse, iter_swap, fill, replace, replace_if and copying into the
   list. Neither do `for (auto& node : ...)` and generic `auto&` callbacks, so callers name the
   node type. */
template <typename Node>
class node_ref {
public:
    // Refers to `*node`, which must not be null.
    constexpr explicit node_ref(Node* node) noexcept
        : node_(node)
    {
    }

    // A copy refers to the same node; there is no assignment.
    constexpr node_ref(const node_ref& other) noexcept = default;
    node_ref& operator=(const node_ref& other) = delete;

    // The node, for binding to `Node&` or `const Node&`.
    constexpr operator Node&() const noexcept { return *node_; }

    // The node's address; never null.
    constexpr Node* get() const noexcept { return node_; }

private:
    // The node referred to; never null.
    Node* node_;
};

} // namespace intrusive

#endif /* INTRUSIVE_NODE_REF_H */
