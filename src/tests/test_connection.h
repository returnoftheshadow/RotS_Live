/* test_connection.h */

#ifndef LOGIN_TESTS_TEST_CONNECTION_H
#define LOGIN_TESTS_TEST_CONNECTION_H

#include "login_test_fixtures.h"

#include "db.h"
#include "interpre.h"
#include "protocol.h"
#include "structs.h"

#include <cstdio>
#include <string>
#include <string_view>

extern struct txt_block* bufpool;

namespace login_tests {

// A connection driven through nanny() one line at a time. When destroyed, it frees the character it
// holds and any original body a switched immortal left with it.
class TestConnection {
public:
    explicit TestConnection(int state)
    {
        connection.descriptor = NO_SOCKET;
        connection.pos = -1;
        connection.connected = state;
        std::snprintf(connection.account_name, sizeof(connection.account_name), "%s", ACCOUNT_NAME);
        std::snprintf(connection.host, sizeof(connection.host), "%s", CONNECTION_HOST);
        clear_output();
    }

    ~TestConnection()
    {
        release_large_output_buffer();
        if (connection.pProtocol != nullptr) {
            ProtocolDestroy(connection.pProtocol);
        }
        free_owned_character(connection.character);
        free_owned_character(connection.original);
    }

    TestConnection(const TestConnection&) = delete;
    TestConnection& operator=(const TestConnection&) = delete;

    // Sends one line of player input, after discarding the output of the previous line.
    void send(std::string_view line)
    {
        clear_output();
        std::string input(line);
        nanny(&connection, input.data());
    }

    // The text written to the connection since the last line was sent.
    std::string output() const
    {
        return connection.output;
    }

    // The connection's login state.
    int state() const
    {
        return connection.connected;
    }

    // The character the connection holds, or null.
    char_data* character() const
    {
        return connection.character;
    }

    // The game's record of the connection, for arranging a test.
    descriptor_data& descriptor()
    {
        return connection;
    }

    // Empties the output and returns the connection to its small buffer, as sending a line does.
    void clear_output()
    {
        release_large_output_buffer();
        connection.output = connection.small_outbuf;
        connection.small_outbuf[0] = '\0';
        connection.bufptr = 0;
        connection.bufspace = SMALL_BUFSIZE - 1;
    }

private:
    // Frees a character this connection still points at.
    void free_owned_character(char_data*& character)
    {
        if (character == nullptr) {
            return;
        }
        if (character->desc == &connection) {
            character->desc = nullptr;
        }
        free_char(character);
        character = nullptr;
    }

    // Returns the large output buffer, if write_to_output switched to one, to the shared pool.
    void release_large_output_buffer()
    {
        if (connection.large_outbuf != nullptr) {
            connection.large_outbuf->next = bufpool;
            bufpool = connection.large_outbuf;
            connection.large_outbuf = nullptr;
        }
    }

    // The game's record of the connection, which nanny() reads and writes.
    descriptor_data connection { };
};

} // namespace login_tests

#endif /* LOGIN_TESTS_TEST_CONNECTION_H */
