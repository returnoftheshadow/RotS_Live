#pragma once

#include "structs.h"

namespace player_spec {
class wild_fighting_handler {
public:
    wild_fighting_handler(char_data* in_character);

    void on_unit_killed(const char_data* victim);
    void update_health(int new_value);
    void update_tactics(int new_value);

    int do_rush(int starting_damage) const;

    float get_wild_swing_damage_multiplier() const;
    bool has_bonus_attack_speed() const;
    float get_attack_speed_multiplier() const;

    void on_enter_rage();

private:
    float get_rush_chance() const;

    char_data* character = nullptr;
    game_types::player_specs spec;
    int tactics = -1;
    int max_health = 0;
    int current_health = 0;
    float health_percentage = 0.0f;
};

// class that wraps a character pointer and answers weaponmaster spec questions about them
class weapon_master_handler {
public:
    weapon_master_handler(char_data* in_character);
    weapon_master_handler(char_data* in_character, const obj_data* in_weapon);

    float get_attack_speed_multiplier() const;
    int get_total_damage(int starting_damage) const;
    int do_on_damage_rolled(int damage_roll, char_data* victim);
    bool ignores_shields(char_data* victim);
    bool ignores_armor(char_data* victim);
    bool does_spear_proc(char_data* victim);
    void regain_energy(char_data* victim);
    void do_on_damage_dealt(int damage, char_data* victim);
    int get_bonus_OB() const;
    int get_bonus_PB() const;
    int append_score_message(char* message_buffer) const;

private:
    bool does_sword_proc() const;

    char_data* character = nullptr;
    const obj_data* weapon = nullptr;
    game_types::player_specs spec;
    game_types::weapon_type weapon_type;
};

class battle_mage_handler {
public:
    battle_mage_handler(const char_data* in_character);
    int get_bonus_spell_pen(int spell_pen) const;
    int get_bonus_spell_power(int spell_power) const;
    bool does_spell_get_interrupted() const;
    bool does_armor_fail_spell() const;
    bool can_prepare_spell() const;
    bool does_mental_attack_interrupt_spell() const;

private:
    char_data* character = nullptr;
    bool is_battle_spec = false;
    int tactics = 0;
    int mage_level = 0;
    int warrior_level = 0;
    const float base_chance = 0.25;
};

} // namespace player_spec
