#include "skill_timer.h"
#include "char_utils.h"
#include "structs.h"

template <>
game_timer::skill_timer* world_singleton<game_timer::skill_timer>::m_pInstance(0);

template <>
bool world_singleton<game_timer::skill_timer>::m_bDestroyed(false);

namespace game_timer {
void skill_timer::add_skill_timer(const char_data& ch, const int skill_id, const int counter)
{
    if (utils::is_npc(ch) || !is_skill_allowed(ch, skill_id)) {
        return;
    }

    int player_id = utils::get_idnum(ch);
    skill_data data;
    data.player_id = player_id;
    data.skill_id = skill_id;
    data.counter = counter;
    m_skill_timer.push_back(data);
    add_global_cooldown(player_id);
}

int skill_timer::report_skill_status(int player_id, char* buffer)
{
    char str[255];
    for (int i = 0; i < m_skill_timer.size(); ++i) {
        auto& data = m_skill_timer[i];

        if (data.player_id == player_id && data.skill_id != GLOBAL_SKILL) {
            sprintf(str, "%-30s %-3d (seconds)\n\r", utils::get_skill_name(data.skill_id), data.counter);
            sprintf(buffer, "%s%s", buffer, str);
        }
    }
    return 1;
}

void skill_timer::update_skill_timer()
{
    for (int i = 0; i < m_skill_timer.size();) {
        auto& data = m_skill_timer[i];
        if (data.counter > 0) {
            data.counter -= 1;
            ++i;
        } else {
            m_skill_timer.erase(m_skill_timer.begin() + i);
        }
    }
}

void skill_timer::add_global_cooldown(int ch_id)
{
    skill_data data;
    data.player_id = ch_id;
    data.skill_id = GLOBAL_SKILL;
    data.counter = GLOBAL_COOLDOWN_COUNTER;
    m_skill_timer.push_back(data);
}

bool skill_timer::is_skill_allowed(const char_data& ch, const int skill_id)
{
    if (utils::is_npc(ch)) {
        return true;
    }

    int player_id = utils::get_idnum(ch);

    for (int i = 0; i < m_skill_timer.size(); ++i) {
        auto data = m_skill_timer[i];
        if (data.player_id == player_id && (data.skill_id == skill_id || data.skill_id == GLOBAL_SKILL)) {
            return false;
        }
    }

    return true;
}
}
