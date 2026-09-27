#pragma once

#include "../structs.h"

#include <gtest/gtest.h>

#include <initializer_list>

extern char_data* character_list;

namespace test_support {

// Makes `characters` the whole global character_list for the scope and restores the previous list
// on exit. extract_char() aborts the process when an NPC or a linkless player it removes is not on
// the list, so every character a test can kill that way belongs here. The list is built by
// prepending, so the last character given heads it. A null character is reported as a test failure
// and skipped.
class ScopedCharacterList {
  public:
    explicit ScopedCharacterList(std::initializer_list<char_data*> characters)
        : m_previous(character_list) {
        character_list = nullptr;
        for (char_data* character : characters) {
            if (character == nullptr) {
                ADD_FAILURE() << "ScopedCharacterList: null character";
                continue;
            }
            character->next = character_list;
            character_list = character;
        }
    }
    ~ScopedCharacterList() { character_list = m_previous; }
    ScopedCharacterList(const ScopedCharacterList&) = delete;
    ScopedCharacterList& operator=(const ScopedCharacterList&) = delete;

  private:
    char_data* m_previous; // character_list before the scope
};

} // namespace test_support
