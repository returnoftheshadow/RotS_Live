#pragma once

#include "base_utils.h"

struct char_data;
struct weather_data;
struct room_data;
struct obj_data;

namespace game_rules {
class combat_manager {
public:
    // Create a new Singleton and store a pointer to it in m_pInstance.
    static void create(const weather_data& weather, const room_data* world)
    {
        // Task: initialize m_pInstance.  Ensure there is only one.
        static combat_manager theInstance(weather, world);
        m_pInstance = &theInstance;
    }

    /// Gets the instance of the Global Library Manager.
    static combat_manager& instance()
    {
        if (!m_pInstance) {
            if (m_bDestroyed) {
                // Log that the WaitList has been destroyed.
            } else {
                // Log that create hasn't been called yet.
            }
        }
        return *m_pInstance;
    }

    // Structure to hold the OB Roll data.
    struct ob_roll {
    public:
        ob_roll()
            : m_is_crit(0)
            , m_ob_value(0.0)
        {
        }
        ob_roll(bool is_crit, double ob)
            : m_is_crit(is_crit)
            , m_ob_value(ob)
        {
        }

        bool is_crit() { return m_is_crit; }
        double get_ob_value() { return m_ob_value; }

    private:
        bool m_is_crit;
        double m_ob_value;
    };

    ob_roll roll_ob(const char_data* attacker);

    bool can_attack(const char_data* attacker, const char_data* victim);
    bool is_hit_accurate(const char_data& attacker);

    // This will return the character's 'left-over' OB if their strike hits.  If this value
    // is less than 1, then the character missed.
    double offense_if_weapon_hits(char_data* attacker, char_data* victim, bool hit_accurate);

    double get_evasion_malus(const char_data& attacker, const char_data& victim);

    void on_weapon_hit(char_data* attacker, char_data* victim, bool hit_accurate, double remaining_ob);

    void apply_weapon_damage(char_data* attacker, const char_data* victim, double damage);
    bool apply_damage(const char_data* attacker, const char_data* victim, double damage, int attack_type, int hit_location);

private:
    // Returns true if a riposte occurs.
    bool does_victim_riposte(const char_data* attacker, const char_data* riposter);
    void do_riposte(char_data* attacker, char_data* riposter);
    void check_grip(char_data* character, const obj_data* weapon);

    double calculate_weapon_damage(const char_data& attacker);
    int get_weapon_skill_type(const char_data& attacker);
    double calculate_hit_damage(const char_data& attacker, bool hit_accurate, double weapon_damage, double remaining_ob);

    bool does_find_weakness(const char_data& attacker);
    bool does_rush(const char_data& attacker);

    double apply_armor_reduction(const char_data* attacker, const char_data* victim, double damage, int weapon_type, int hit_location);

    // Deleted functions.
    // Don't put definitions in here so trying to do them will cause a compile error.
    combat_manager(const combat_manager&);
    combat_manager& operator=(const combat_manager&);

    /// Constructor/Destructor
    combat_manager(const weather_data& weather, const room_data* world);
    ~combat_manager();

    /// Instance of the singleton, and lifetime management.
    static combat_manager* m_pInstance;
    static bool m_bDestroyed;

    const weather_data& m_weather;
    const room_data* m_world;
};
}
