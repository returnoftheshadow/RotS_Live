#ifndef OWNED_CHARACTER_H
#define OWNED_CHARACTER_H

#include "db.h" /* clear_char, free_char */
#include "structs.h" /* char_data, MOB_VOID */
#include "utils.h" /* CREATE */

/* Owns a character made the way the game makes one (allocated with CREATE, then cleared with
 * clear_char) and frees it with free_char when it goes out of scope. The character must be off
 * every global list by then: character_list, a room, a follower list, the combat and waiting lists.
 * A character the game is to free instead, through extract_char, a descriptor or its own
 * free_char, is handed over with release(). Ownership moves with the object and is never shared. */
class owned_character {
public:
    /* Makes the character: a zeroed allocation that clear_char sets up in 'mode', MOB_VOID for a
     * player and MOB_ISNPC for a mob, which gets no skill or knowledge arrays. */
    explicit owned_character(int mode = MOB_VOID)
        : m_character(nullptr)
    {
        CREATE(m_character, char_data, 1);
        clear_char(m_character, mode);
    }

    /* Frees the character with free_char, unless it has been released or moved away. */
    ~owned_character()
    {
        if (m_character != nullptr) {
            free_char(m_character);
        }
    }

    /* Takes the character from 'other', which is left owning nothing. */
    owned_character(owned_character&& other) noexcept
        : m_character(other.m_character)
    {
        other.m_character = nullptr;
    }

    /* Frees the character held now, then takes the one from 'other', which is left owning
     * nothing. */
    owned_character& operator=(owned_character&& other) noexcept
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

    owned_character(const owned_character&) = delete;
    owned_character& operator=(const owned_character&) = delete;

    /* The character, so the object passes straight to the game's functions that take a
     * char_data*. Null once released. */
    operator char_data*() const { return m_character; }

    /* The character's members. The character must not have been released. */
    char_data* operator->() const { return m_character; }

    /* The character, for functions that take a char_data&. It must not have been released. */
    char_data& operator*() const { return *m_character; }

    /* The character, or null once released. */
    char_data* get() const { return m_character; }

    /* Gives the character up without freeing it and returns it; whoever receives it frees it,
     * usually the game. The object owns nothing afterwards. */
    [[nodiscard]] char_data* release()
    {
        char_data* const released_character = m_character;
        m_character = nullptr;
        return released_character;
    }

private:
    // The character this object frees on destruction; null once released or moved away.
    char_data* m_character;
};

#endif
