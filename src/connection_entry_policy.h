/* connection_entry_policy.h */

#ifndef CONNECTION_ENTRY_POLICY_H
#define CONNECTION_ENTRY_POLICY_H

#include <string_view>

struct descriptor_data;

// Decides whether a line typed at the account email prompt takes its connection into the game
// by a way other than the login dialogue.
class ConnectionEntryPolicy {
public:
    virtual ~ConnectionEntryPolicy() = default;

    // Offered each line a connection sends while at the account email prompt, before the login
    // dialogue reads it. Returns true when the policy has handled the line: the connection is
    // then playing, or has been told why not and is closing. Returns false to leave the line to
    // the login dialogue, untouched.
    [[nodiscard]] virtual bool try_enter(descriptor_data& connection, std::string_view line) = 0;
};

#endif /* CONNECTION_ENTRY_POLICY_H */
