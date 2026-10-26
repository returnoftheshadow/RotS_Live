#include "death_extraction_hold.h"

#include <cassert>

death_extraction_hold* death_extraction_hold::newest = nullptr;

death_extraction_hold::death_extraction_hold(const char_data* dying)
    : m_dying(dying)
    , m_older(newest)
{
    newest = this;
}

/* Holds are local variables, so they go in the reverse order of their making. */
death_extraction_hold::~death_extraction_hold()
{
    assert(newest == this);
    newest = m_older;
}

void death_extraction_hold::note_freed(const char_data* character)
{
    for (death_extraction_hold* hold = newest; hold; hold = hold->m_older) {
        if (hold->m_dying == character) {
            hold->m_dying = nullptr;
        }
    }
}

bool death_extraction_hold::hold_extraction(const char_data* character)
{
    for (death_extraction_hold* hold = newest; hold; hold = hold->m_older) {
        if (hold->m_dying == character) {
            hold->m_requested = true;
            return true;
        }
    }
    return false;
}
