/* scoped_empty_player_table.h */

#ifndef LOGIN_TESTS_SCOPED_EMPTY_PLAYER_TABLE_H
#define LOGIN_TESTS_SCOPED_EMPTY_PLAYER_TABLE_H

#include "structs.h"

#include <cstdlib>

extern struct player_index_element* player_table;
extern int top_of_p_table;

namespace login_tests {

// An empty player table for the life of the scope. Entries the game adds are freed afterwards.
class ScopedEmptyPlayerTable {
public:
    ScopedEmptyPlayerTable()
        : saved_table(player_table)
        , saved_top(top_of_p_table)
    {
        player_table = nullptr;
        top_of_p_table = -1;
    }

    ~ScopedEmptyPlayerTable()
    {
        for (int index = 0; index <= top_of_p_table; ++index) {
            std::free(player_table[index].name);
        }
        std::free(player_table);
        player_table = saved_table;
        top_of_p_table = saved_top;
    }

    ScopedEmptyPlayerTable(const ScopedEmptyPlayerTable&) = delete;
    ScopedEmptyPlayerTable& operator=(const ScopedEmptyPlayerTable&) = delete;

private:
    // The player table before the scope, restored when it ends.
    player_index_element* saved_table;
    // The index of its last entry.
    int saved_top;
};

} // namespace login_tests

#endif /* LOGIN_TESTS_SCOPED_EMPTY_PLAYER_TABLE_H */
