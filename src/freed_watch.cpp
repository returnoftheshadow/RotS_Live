#include "freed_watch.h"

#include <algorithm>
#include <cassert>

freed_watch* freed_watch::newest = nullptr;
freed_log* freed_log::newest = nullptr;
death_watch* death_watch::newest = nullptr;

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
    freed_log::note_freed_address(address);
}

freed_log::freed_log()
    : m_older(newest)
{
    newest = this;
}

/* Logs are local variables, so they go in the reverse order of their making, as watches do. */
freed_log::~freed_log()
{
    assert(newest == this);
    newest = m_older;
}

bool freed_log::contains(const void* address) const
{
    return std::find(m_freed.begin(), m_freed.end(), address) != m_freed.end();
}

void freed_log::note_freed(const affected_type* affect) { note_freed_address(affect); }

void freed_log::note_freed(const universal_list* entry) { note_freed_address(entry); }

/* Affects and list entries are separate allocations from objects and characters too, so one
 * address names at most one thing that exists. */
void freed_log::note_freed_address(const void* address)
{
    if (!address) {
        return;
    }
    for (freed_log* recording = newest; recording; recording = recording->m_older) {
        recording->m_freed.push_back(address);
    }
}

death_watch::death_watch(const char_data* character)
    : m_watched(character)
    , m_older(newest)
{
    newest = this;
}

/* Watches are local variables, so they go in the reverse order of their making, as freed_watch's
 * do. */
death_watch::~death_watch()
{
    assert(newest == this);
    newest = m_older;
}

void death_watch::note_died(const char_data* character)
{
    if (!character) {
        return;
    }
    for (death_watch* watch = newest; watch; watch = watch->m_older) {
        if (watch->m_watched == character) {
            watch->m_died = true;
        }
    }
}
