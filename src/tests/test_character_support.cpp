#include "test_character_support.h"

#include "../db.h"
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

} // namespace test_support
