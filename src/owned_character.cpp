/* owned_character.cpp */

#include "owned_character.h"

#include "db.h" /* clear_char, free_char */
#include "utils.h" /* CREATE */

owned_character::owned_character(int mode)
    : m_character(nullptr)
{
    CREATE(m_character, char_data, 1);
    clear_char(m_character, mode);
}

owned_character::~owned_character()
{
    if (m_character != nullptr) {
        free_char(m_character);
    }
}

owned_character::owned_character(owned_character&& other) noexcept
    : m_character(other.m_character)
{
    other.m_character = nullptr;
}

owned_character& owned_character::operator=(owned_character&& other) noexcept
{
    if (this != &other) {
        if (m_character != nullptr) {
            free_char(m_character);
        }
        m_character = other.m_character;
        other.m_character = nullptr;
    }
    return *this;
}

owned_character::operator char_data*() const
{
    return m_character;
}

char_data* owned_character::operator->() const
{
    return m_character;
}

char_data& owned_character::operator*() const
{
    return *m_character;
}

char_data* owned_character::get() const
{
    return m_character;
}

char_data* owned_character::release()
{
    char_data* const released_character = m_character;
    m_character = nullptr;
    return released_character;
}
