#pragma once

#include <functional>

struct char_data;
struct waiting_type;

namespace test_support {

// Gives world[room_number] a special procedure for the scope and restores the room's previous one
// on exit. Whenever special() calls it with `callflag` (SPECIAL_ENTER, SPECIAL_COMMAND, ...), the
// special runs `action` on the character that set it off and reports the call handled, as a real
// special that kills must: special() then returns without reading the character again. Calls with
// any other callflag pass through unhandled. The action may kill the character, add or remove its
// affects, or do anything else a special could; it runs on every matching call, so an action meant
// for one particular call counts the calls itself. Scopes on different rooms may be active at
// once; a second scope on a room that already has one is reported as a test failure and does
// nothing.
class ScopedRoomSpecial {
  public:
    ScopedRoomSpecial(int room_number, int callflag, std::function<void(char_data*)> action);
    ~ScopedRoomSpecial();
    ScopedRoomSpecial(const ScopedRoomSpecial&) = delete;
    ScopedRoomSpecial& operator=(const ScopedRoomSpecial&) = delete;

  private:
    using room_special = int (*)(char_data*, char_data*, int, char*, int, waiting_type*);

    // The installed scope on world[room_number], or null.
    static ScopedRoomSpecial* find_active(int room_number);

    // The special installed on every room with an active scope. It finds the scope by the room of
    // the character that set it off, the room special() consults for every local call.
    static int dispatch(char_data* host, char_data* character, int command, char* argument,
                        int callflag, waiting_type* wait_list);

    int m_room_number;                        // the world[] index whose special this scope owns
    int m_callflag;                           // the only callflag the action answers
    std::function<void(char_data*)> m_action; // what the special does to the character
    room_special m_previous_special;          // the room's special before the scope
    bool m_installed;                         // false when the room already had an active scope
};

} // namespace test_support
