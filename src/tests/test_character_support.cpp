#include "test_character_support.h"

#include "../db.h"
#include "../handler.h"
#include "../structs.h"
#include "../utils.h"

namespace test_support {

char_data* allocate_test_character(int clear_mode)
{
    char_data* character = nullptr;
    CREATE(character, char_data, 1);
    clear_char(character, clear_mode);
    return character;
}

void release_test_character(char_data* character)
{
    free_char(character);
}

ScopedCharExists::ScopedCharExists(char_data& character, int abs_number)
    : m_character(character)
{
    character.abs_number = abs_number;
    set_char_exists(abs_number, &character);
}

ScopedCharExists::~ScopedCharExists()
{
    remove_char_exists(m_character.abs_number);
}

ScopedAffectCleanup::~ScopedAffectCleanup()
{
    while (m_character.affected) {
        affect_remove(&m_character, m_character.affected);
    }
}

} // namespace test_support
