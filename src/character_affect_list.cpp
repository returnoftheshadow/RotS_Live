#include "character_affect_list.h"

#include "structs.h"
#include "utils.h"

void character_affect_list::push_front(affected_type* node) {
    if (node == nullptr) {
        log("SYSERR: character_affect_list::push_front: null node");
        return;
    }
    node->next = m_head;
    m_head = node;
}

// The search has no length cap, unlike the MAX_AFFECT-bounded list walks elsewhere:
// affect_join() can hand over a node sitting past MAX_AFFECT entries, and it must be removable.
bool character_affect_list::unlink(affected_type* node) {
    if (node == nullptr) {
        log("SYSERR: character_affect_list::unlink: null node");
        return false;
    }
    affected_type** link = &m_head;
    while (*link != nullptr && *link != node) {
        link = &(*link)->next;
    }
    if (*link == nullptr) {
        return false;
    }
    *link = node->next;
    ++m_removal_count;
    return true;
}

bool character_affect_list::contains(int affect_type) const {
    int visited = 0;
    for (const affected_type* node = m_head; node != nullptr && visited < MAX_AFFECT;
         node = node->next, ++visited) {
        if (node->type == affect_type) {
            return true;
        }
    }
    return false;
}
