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

void make_stack_npc(char_data& character, char_prof_data& profs)
{
    character.profs = &profs;
    character.specials2.act = MOB_ISNPC;
    character.nr = -1;
    character.player.race = RACE_HUMAN;
    character.player.level = 10;
    character.specials.position = POSITION_STANDING;
    character.specials.fighting = nullptr;
}

void make_sturdy_stack_npc(char_data& character, char_prof_data& profs)
{
    make_stack_npc(character, profs);
    character.abilities.hit = 500;
    character.tmpabilities.hit = 500;
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
