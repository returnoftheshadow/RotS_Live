#include "scoped_room_special.h"

#include "../structs.h"
#include "test_world_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <utility>
#include <vector>

extern room_data world;

namespace test_support {

namespace {

// Every scope whose special is installed, so dispatch() can reach the one for a room: a room's
// special is a plain function pointer and carries no state of its own.
std::vector<ScopedRoomSpecial*>& active_scopes() {
    static std::vector<ScopedRoomSpecial*> scopes;
    return scopes;
}

} // namespace

ScopedRoomSpecial::ScopedRoomSpecial(int room_number, int callflag,
                                     std::function<void(char_data*)> action)
    : m_room_number(room_number), m_callflag(callflag), m_action(std::move(action)),
      m_previous_special(nullptr), m_installed(false) {
    ensure_test_world(room_number);
    if (find_active(room_number) != nullptr) {
        ADD_FAILURE() << "ScopedRoomSpecial: room " << room_number << " already has a scope";
        return;
    }
    m_previous_special = world[room_number].funct;
    world[room_number].funct = &ScopedRoomSpecial::dispatch;
    std::vector<ScopedRoomSpecial*>& scopes = active_scopes();
    scopes.push_back(this);
    m_installed = true;
}

ScopedRoomSpecial::~ScopedRoomSpecial() {
    if (!m_installed) {
        return;
    }
    world[m_room_number].funct = m_previous_special;
    std::vector<ScopedRoomSpecial*>& scopes = active_scopes();
    scopes.erase(std::remove(scopes.begin(), scopes.end(), this), scopes.end());
}

ScopedRoomSpecial* ScopedRoomSpecial::find_active(int room_number) {
    std::vector<ScopedRoomSpecial*>& scopes = active_scopes();
    const auto same_room = [room_number](const ScopedRoomSpecial* scope) -> bool {
        return scope->m_room_number == room_number;
    };
    const auto found = std::find_if(scopes.begin(), scopes.end(), same_room);
    if (found == scopes.end()) {
        return nullptr;
    }
    return *found;
}

int ScopedRoomSpecial::dispatch(char_data* /*host*/, char_data* character, int /*command*/,
                                char* /*argument*/, int callflag, waiting_type* /*wait_list*/) {
    // special() calls a room special with a null character for some callflags (SPECIAL_TARGET,
    // SPECIAL_DAMAGE), so the callflag is checked before the character is.
    const std::vector<ScopedRoomSpecial*>& scopes = active_scopes();
    const auto answers_callflag = [callflag](const ScopedRoomSpecial* scope) -> bool {
        return scope->m_callflag == callflag;
    };
    if (std::none_of(scopes.begin(), scopes.end(), answers_callflag)) {
        return 0;
    }
    if (character == nullptr) {
        ADD_FAILURE() << "ScopedRoomSpecial: special() passed a null character for callflag "
                      << callflag;
        return 0;
    }
    ScopedRoomSpecial* const scope = find_active(character->in_room);
    if (scope == nullptr || callflag != scope->m_callflag) {
        return 0;
    }
    scope->m_action(character);
    return 1;
}

} // namespace test_support
