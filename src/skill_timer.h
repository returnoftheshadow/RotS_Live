#pragma once

#include "singleton.h"
#include <cstddef>
#include <vector>

struct char_data;

namespace game_timer {
class skill_timer : public world_singleton<skill_timer> {
public:
    // Called to add player and skill to cooldown list.
    void add_skill_timer(const char_data& ch, const int skill_id, const int counter);
    // Returns true if the player and skill aren't on cooldown.
    bool is_skill_allowed(const char_data& ch, const int skill_id);
    // Updates the skill timer list and removes any that have expired.
    void update_skill_timer();
    // Appends all tracked skills for a specific player into a caller-provided
    // NUL-terminated buffer, respecting the provided capacity.
    void report_skill_status(int player_id, char* buffer, std::size_t buffer_size);

private:
    friend class world_singleton<skill_timer>;
    const int GLOBAL_SKILL = -1;
    const int GLOBAL_COOLDOWN_COUNTER = 2;
    struct skill_data {
        int player_id;
        int skill_id;
        int counter;

        skill_data()
            : player_id(0)
            , skill_id(0)
            , counter(0) {};
    };
    std::vector<skill_data> m_skill_timer;
    void add_global_cooldown(int ch_id);
    skill_timer(const weather_data* weather, const room_data* world)
        : world_singleton<skill_timer>(weather, world) {};
};
}
