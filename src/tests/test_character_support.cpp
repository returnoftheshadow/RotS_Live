#include "test_character_support.h"

#include "../db.h"
#include "../handler.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

namespace test_support {

namespace {

// The level fill_stack_npc() gives; any mortal level serves the affect tests.
constexpr int kStackNpcLevel = 10;

} // namespace

char_data* allocate_test_character(int clear_mode) {
    char_data* character = nullptr;
    CREATE(character, char_data, 1);
    clear_char(character, clear_mode);
    return character;
}

void release_test_character(char_data* character) {
    if (character == nullptr) {
        ADD_FAILURE() << "release_test_character: null character";
        return;
    }
    free_char(character);
}

void fill_stack_npc(char_data& out_character, char_prof_data& profs) {
    out_character.profs = &profs;
    out_character.specials2.act = MOB_ISNPC;
    out_character.nr = -1;
    out_character.player.race = RACE_HUMAN;
    out_character.player.level = kStackNpcLevel;
    out_character.specials.position = POSITION_STANDING;
    out_character.specials.fighting = nullptr;
}

void fill_sturdy_stack_npc(char_data& out_character, char_prof_data& profs) {
    fill_stack_npc(out_character, profs);
    out_character.abilities.hit = kSturdyNpcHitPoints;
    out_character.tmpabilities.hit = kSturdyNpcHitPoints;
}

ScopedCharExists::ScopedCharExists(char_data& character, int abs_number) : m_character(character) {
    character.abs_number = abs_number;
    set_char_exists(abs_number, &character);
}

ScopedCharExists::~ScopedCharExists() { remove_char_exists(m_character.abs_number); }

ScopedAffectCleanup::~ScopedAffectCleanup() {
    while (m_character.affected) {
        affect_remove(&m_character, m_character.affected);
    }
}

} // namespace test_support
