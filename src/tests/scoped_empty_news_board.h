/* scoped_empty_news_board.h */

#ifndef LOGIN_TESTS_SCOPED_EMPTY_NEWS_BOARD_H
#define LOGIN_TESTS_SCOPED_EMPTY_NEWS_BOARD_H

#include "boards.h"
#include "structs.h"

#include <cstdlib>

extern struct board_info_type* news_board;

namespace login_tests {

// An empty news board for the life of the scope; entering the game counts its unread news.
class ScopedEmptyNewsBoard {
public:
    ScopedEmptyNewsBoard()
        : saved_board_count(board_info_type::num_of_boards)
        , saved_news_board(news_board)
    {
        board.lnum = 0;
        news_board = &board;
    }

    ~ScopedEmptyNewsBoard()
    {
        news_board = saved_news_board;
        board_info_type::num_of_boards = saved_board_count;
        std::free(board.msg_index);
    }

    ScopedEmptyNewsBoard(const ScopedEmptyNewsBoard&) = delete;
    ScopedEmptyNewsBoard& operator=(const ScopedEmptyNewsBoard&) = delete;

private:
    // The board count before the scope; constructing a board increments it.
    byte saved_board_count;
    // The news board before the scope.
    board_info_type* saved_news_board;
    // The empty board that stands in for the news board.
    board_info_type board;
};

} // namespace login_tests

#endif /* LOGIN_TESTS_SCOPED_EMPTY_NEWS_BOARD_H */
