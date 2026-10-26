#include "freed_watch.h"

#include <cassert>

freed_watch* freed_watch::newest = nullptr;

freed_watch::freed_watch(const obj_data* object)
    : freed_watch(static_cast<const void*>(object))
{
}

freed_watch::freed_watch(const char_data* character)
    : freed_watch(static_cast<const void*>(character))
{
}

freed_watch::freed_watch(const void* watched)
    : m_watched(watched)
    , m_older(newest)
{
    newest = this;
}

/* Watches are local variables, so they go in the reverse order of their making and this one is
 * the newest when it goes. */
freed_watch::~freed_watch()
{
    assert(newest == this);
    newest = m_older;
}

void freed_watch::note_freed(const obj_data* object) { note_freed_address(object); }

void freed_watch::note_freed(const char_data* character) { note_freed_address(character); }

/* Objects and characters are separate allocations, so one address never names both while either
 * exists; a single list serves both kinds. */
void freed_watch::note_freed_address(const void* address)
{
    if (!address) {
        return;
    }
    for (freed_watch* watch = newest; watch; watch = watch->m_older) {
        if (watch->m_watched == address) {
            watch->m_freed = true;
        }
    }
}
