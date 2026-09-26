#pragma once

struct affected_type;

// The head of one character's affect list, plus a count of the nodes unlinked from it, so a
// caller holding a node pointer can tell whether that node may since have been freed. It also
// answers whether the list holds an affect of a given type. It does not own, allocate or free
// nodes.
//
// The head can only be written through push_front() and unlink(): there is no construction or
// assignment from affected_type*, so `ch->affected = node` does not compile. Two holes stay
// open. A node's `next` is public, so splicing a node out of the middle of the list by hand still
// compiles; such code must call unlink() instead, or the removal count misses the removal. And
// the implicit copy assignment, kept so the type stays trivial, overwrites the head and the count
// and leaves both lists sharing the same nodes.
//
// The type is trivial so that a zero-filled char_data (clear_char()'s memset) is an empty list
// with a zero count, and whole char_data copies (a mob instantiated from its prototype) stay
// valid. Do not add constructors, default member initializers or special members.
class character_affect_list {
  public:
    // The head node, or null when the list is empty. Implicit rather than an explicit getter,
    // which would mean editing every existing reader of `ch->affected` (null tests,
    // `af = ch->affected`, comparisons, arguments).
    operator affected_type*() const { return m_head; }
    // The head node; the caller must know the list is not empty.
    affected_type* operator->() const { return m_head; }

    // Links `node` at the head, setting its `next` to the previous head. A null `node` logs a
    // SYSERR and changes nothing. Does not change removal_count().
    void push_front(affected_type* node);
    // Unlinks `node` from wherever it sits in the list and raises removal_count() by one; the
    // node itself is left for the caller to free. Returns false and changes nothing when `node`
    // is not on the list (the caller logs that) or is null (logged here as a SYSERR).
    bool unlink(affected_type* node);
    // How many nodes unlink() has removed from this list.
    long removal_count() const { return m_removal_count; }
    // Whether an affect of `affect_type` is on the list. Like affected_by_spell(), it looks at
    // only the first MAX_AFFECT nodes, so a node beyond them is not found.
    bool contains(int affect_type) const;

  private:
    affected_type* m_head; // the newest affect; each node's `next` leads to the older ones
    long m_removal_count;  // raised by every successful unlink(); never lowered
};
