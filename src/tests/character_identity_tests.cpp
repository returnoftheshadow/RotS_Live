#include "../character_identity.h"
#include "../handler.h"
#include "../structs.h"
#include "test_character_support.h"

#include <gtest/gtest.h>

#include <type_traits>

namespace {

// Registry slots owned by this file, far above the slots register_npc_char() hands out from 0 and
// clear of the ones other suites claim (affect_update_tests.cpp: MAX_CHARACTERS - 201 to - 205;
// char_utils_tests.cpp: - 17 and - 18).
constexpr int kIdentitySlot = MAX_CHARACTERS - 301;
constexpr int kSecondIdentitySlot = MAX_CHARACTERS - 302;

static_assert(std::is_trivially_copyable_v<character_identity>,
              "a character_identity is copied by value");

} // namespace

TEST(CharacterIdentity, ACapturedCharacterResolvesToItself) {
    char_data character{};
    char_prof_data profs{};
    test_support::fill_stack_npc(character, profs);
    test_support::ScopedCharExists registration(character, kIdentitySlot);

    const character_identity identity = character_identity::capture(character);

    EXPECT_EQ(identity.resolve(), &character);
}

TEST(CharacterIdentity, ACharacterOutsideEveryRoomStillResolves) {
    char_data character{};
    char_prof_data profs{};
    test_support::fill_stack_npc(character, profs);
    character.in_room = NOWHERE;
    test_support::ScopedCharExists registration(character, kIdentitySlot);

    const character_identity identity = character_identity::capture(character);

    EXPECT_EQ(identity.resolve(), &character);
}

TEST(CharacterIdentity, AnUnregisteredSlotDoesNotResolve) {
    char_data character{};
    char_prof_data profs{};
    test_support::fill_stack_npc(character, profs);
    test_support::ScopedCharExists registration(character, kIdentitySlot);
    const character_identity identity = character_identity::capture(character);

    remove_char_exists(kIdentitySlot);

    EXPECT_EQ(identity.resolve(), nullptr);
}

TEST(CharacterIdentity, ASlotRegisteredAgainAtTheSameAddressDoesNotResolve) {
    char_data character{};
    char_prof_data profs{};
    test_support::fill_stack_npc(character, profs);
    test_support::ScopedCharExists registration(character, kIdentitySlot);
    const character_identity identity = character_identity::capture(character);

    set_char_exists(kIdentitySlot, &character); // same slot, same address, a new serial

    EXPECT_EQ(identity.resolve(), nullptr);
    const character_identity new_identity = character_identity::capture(character);
    EXPECT_EQ(new_identity.resolve(), &character) << "the new registration itself resolves";
}

TEST(CharacterIdentity, ASlotOwnedByAnotherCharacterDoesNotResolve) {
    char_data character{};
    char_prof_data profs{};
    test_support::fill_stack_npc(character, profs);
    char_data newcomer{};
    char_prof_data newcomer_profs{};
    test_support::fill_stack_npc(newcomer, newcomer_profs);
    test_support::ScopedCharExists registration(character, kIdentitySlot);
    const character_identity identity = character_identity::capture(character);

    remove_char_exists(kIdentitySlot);
    test_support::ScopedCharExists newcomer_registration(newcomer, kIdentitySlot);

    EXPECT_EQ(identity.resolve(), nullptr);
}

TEST(CharacterIdentity, ANeverRegisteredCharacterDoesNotResolve) {
    char_data character{};
    char_prof_data profs{};
    test_support::fill_stack_npc(character, profs);
    character.abs_number = kSecondIdentitySlot;
    remove_char_exists(kSecondIdentitySlot);

    const character_identity identity = character_identity::capture(character);

    EXPECT_EQ(identity.resolve(), nullptr);
}
