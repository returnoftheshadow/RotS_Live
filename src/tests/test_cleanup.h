#pragma once

#include "../db.h"
#include "../interpre.h"
#include "../protocol.h"
#include "../structs.h"
#include "../utils.h"

extern struct txt_block* bufpool;
extern struct player_index_element* player_table;
extern int top_of_p_table;

// Frees the profession, skill and knowledge arrays clear_char gave a character that the test
// owns itself and never hands to free_char, such as a fixture member or a stack character.
// Safe on a zeroed character and on one released already.
inline void release_cleared_character(char_data* character)
{
    RELEASE(character->profs);
    RELEASE(character->skills);
    RELEASE(character->knowledge);
}

// Releases a character on the stack or in a fixture when the scope ends, so the test frees what
// clear_char gave it even if an assertion returns early.
class ClearedCharacterRelease {
public:
    explicit ClearedCharacterRelease(char_data* character)
        : m_character(character)
    {
    }

    ~ClearedCharacterRelease() { release_cleared_character(m_character); }

    ClearedCharacterRelease(const ClearedCharacterRelease&) = delete;
    ClearedCharacterRelease& operator=(const ClearedCharacterRelease&) = delete;

private:
    // The character whose clear_char arrays are freed on destruction; the test owns it.
    char_data* m_character;
};

// Gives back what the game gave a descriptor the test owns and never passes to close_socket, as
// close_socket would: the large output buffer write_to_output switched to goes back to the shared
// pool, as flush_queues returns it, and the protocol state is destroyed. The descriptor is left
// writing to its small buffer again.
inline void release_test_descriptor(descriptor_data* descriptor)
{
    if (descriptor->large_outbuf != nullptr) {
        descriptor->large_outbuf->next = bufpool;
        bufpool = descriptor->large_outbuf;
        descriptor->large_outbuf = nullptr;
        descriptor->output = descriptor->small_outbuf;
        descriptor->bufspace = SMALL_BUFSIZE - 1;
    }
    if (descriptor->pProtocol != nullptr) {
        ProtocolDestroy(descriptor->pProtocol);
        descriptor->pProtocol = nullptr;
    }
}

// Releases a descriptor on the stack or in a fixture when the scope ends, even if an assertion
// returns early.
class TestDescriptorRelease {
public:
    explicit TestDescriptorRelease(descriptor_data* descriptor)
        : m_descriptor(descriptor)
    {
    }

    ~TestDescriptorRelease() { release_test_descriptor(m_descriptor); }

    TestDescriptorRelease(const TestDescriptorRelease&) = delete;
    TestDescriptorRelease& operator=(const TestDescriptorRelease&) = delete;

private:
    // The descriptor whose output buffer and protocol state are given back on destruction.
    descriptor_data* m_descriptor;
};

// Gives the test an empty player index and, on leaving, frees the index the test's logins and
// migrations built (create_entry allocates the table and each entry's name) before putting the
// previous index back.
class ScopedPlayerTableReset {
public:
    ScopedPlayerTableReset()
        : m_previous_player_table(player_table)
        , m_previous_top_of_p_table(top_of_p_table)
    {
        player_table = nullptr;
        top_of_p_table = -1;
    }

    ~ScopedPlayerTableReset()
    {
        if (player_table != nullptr) {
            for (int index = 0; index <= top_of_p_table; ++index)
                RELEASE(player_table[index].name);
            RELEASE(player_table);
        }

        player_table = m_previous_player_table;
        top_of_p_table = m_previous_top_of_p_table;
    }

    ScopedPlayerTableReset(const ScopedPlayerTableReset&) = delete;
    ScopedPlayerTableReset& operator=(const ScopedPlayerTableReset&) = delete;

private:
    // The index in force before the test, put back on leaving.
    player_index_element* m_previous_player_table;
    // The position of its last entry, put back with it.
    int m_previous_top_of_p_table;
};

// Drops every linked-character selection unlock on entry and when the scope ends, so an unlock
// the test grants and never uses is not still pending in a later test or a repeat of this one.
class ScopedSelectionUnlockReset {
public:
    ScopedSelectionUnlockReset() { clear_account_character_selection_unlocks_for_testing(); }

    ~ScopedSelectionUnlockReset() { clear_account_character_selection_unlocks_for_testing(); }

    ScopedSelectionUnlockReset(const ScopedSelectionUnlockReset&) = delete;
    ScopedSelectionUnlockReset& operator=(const ScopedSelectionUnlockReset&) = delete;
};
