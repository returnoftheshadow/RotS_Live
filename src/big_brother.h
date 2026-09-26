#pragma once
#ifndef BIG_BROTHER_H
#define BIG_BROTHER_H

#include "base_utils.h"
#include "singleton.h"

#include <map>
#include <set>
#include <time.h>

#ifndef USE_BIG_BROTHER
#define USE_BIG_BROTHER 1
#endif

struct obj_data;
struct char_data;
struct caster_snapshot;

namespace game_rules {
class big_brother : public world_singleton<big_brother> {
public:
    // Called before any character loots an item.  This enforces our PK loot rules.
    bool on_loot_item(char_data* looter, obj_data* corpse, obj_data* item);

    // Returns true if the victim has the 'looting' flag.
    bool is_target_looting(const char_data* victim) const;

    // Returns true if Big Brother protection applies to this corpse.
    bool is_corpse_protected(const char_data* looter, obj_data* corpse) const;

    // Called before any character attempts to damage or attack another character.
    // This enforces our PK engagement rules.
    bool is_target_valid(char_data* attacker, const char_data* victim) const;

    // Called before any character attempts to damage or attack another character.
    // This enforces our PK engagement rules.
    bool is_target_valid(char_data* attacker, const char_data* victim, int skill_id) const;

    // The engagement rules for an attacker known only by its caster_snapshot, for a spell
    // that outlives its caster (a room affect's tick after the caster quit, died or was
    // purged). Judges as the live overload above would, reading the attacker's side from the
    // snapshot: the same character, a snapshot naming nobody, and every NPC are always
    // valid; otherwise the victim-side rules and the level band apply. A charmed NPC whose
    // master can no longer be reached is judged as a plain NPC, as an uncharmed mob is. A
    // null victim is valid.
    bool is_target_valid(const caster_snapshot& attacker, const char_data* victim, int skill_id) const;

    // Redirects in an attempt to find a suitable target for the attacker in the case
    // that their original target was not valid.  If no suitable target is found, NULL
    // is returned.
    char_data* get_valid_target(char_data* attacker, const char_data* victim, const char* argument) const;

    // Called when a character dies to create information about loot rules.
    void on_character_died(char_data* character, char_data* killer, obj_data* corpse);

    // Called when a character auto-AFKs so that they can [potentially] be protected.
    void on_character_afked(const char_data* character);

    // Called when a character successfully attacks another player.  This is used to
    // track whether or not a character should get AFK protection.
    void on_character_attacked_player(const char_data* attacker, const char_data* victim);

    // When a character disconnects, let the Big Brother system know so that it can
    // clean up any references.
    void on_character_disconnected(const char_data* character);

    // When a corpse decays, let the Big Brother system know so that it will no longer
    // track it.
    void on_corpse_decayed(obj_data* corpse);

    // Alert the Big Brother system that a character has returned to their keyboard.
    void on_character_returned(const char_data* character);

private:
    friend class world_singleton<big_brother>;

    struct player_corpse_data {
        player_corpse_data()
            : num_items_looted(0)
            , max_num_items_looted(2)
            , player_race(0)
            , killer_id(-1)
            , player_id(0)
            , is_npc(false)
            , is_killer_pc(false) {};
        player_corpse_data(char_data* dead_man);
        player_corpse_data(char_data* dead_man, char_data* killer);

        int num_items_looted;
        int max_num_items_looted;
        int player_race;
        int killer_id;
        int player_id;
        bool is_npc;
        bool is_killer_pc;
    };

    typedef std::map<obj_data*, player_corpse_data> corpse_map;
    typedef std::set<const char_data*> character_set;
    typedef std::set<int> character_id_set;
    typedef std::map<const char_data*, tm> time_map;
    typedef std::set<int> skill_id_set;

    // Private constructor that we friend with our parent to grant access.
    big_brother(const weather_data* weather, const room_data* world)
        : world_singleton<big_brother>(weather, world)
    {
#if USE_BIG_BROTHER
        populate_skill_sets();
#endif
    }

    // A player attacker as the victim-side rules see it: the live character, or the snapshot
    // of one who can no longer be reached. Exactly one member is set.
    struct attacker_view {
        const char_data* live; // the attacking character; null when judged from `snapshot`
        const caster_snapshot* snapshot; // the attacker as captured; null when judged from `live`

        // True when `candidate` is the attacker itself.
        bool is(const char_data& candidate) const;

        // The attacker's level as the level-band rule reads it.
        int level_legend_cap() const;
    };

    // The rules that depend on the victim, for an attacker already known to be a player.
    bool is_player_attack_valid(const attacker_view& attacker, const char_data* victim) const;

    // Whether `skill_id` may still be used on a target the engagement rules protect.
    bool is_skill_allowed_on_protected_target(int skill_id, int attacker_race, int victim_race) const;

    // Is the spell being cast or skill being used offensive in nature.
    bool is_skill_offensive(int skill_id) const;

    // Returns true if the victim is within an appropriate level of the attacker.
    bool is_level_range_appropriate(int attacker_level, const char_data* victim) const;

    // Returns true if the victim has been auto-afk'd.
    bool is_target_afk(const char_data* victim) const;

    // Returns true if two targets are on the same side of the race war.
    bool is_same_side_race_war(int attacker_race, int victim_race) const;

    // Logs when an item is looted from a protected corpse.
    void log_item_looted(const char_data* looter, corpse_map::iterator& iter, obj_data* item) const;

    // Removes a character from our afk_characters set.
    void remove_character_from_afk_set(const char_data* character);

    // Removes a character from our looting characters set.
    void remove_character_from_looting_set(int char_id);

#ifdef USE_BIG_BROTHER

    void populate_skill_sets();

    corpse_map m_corpse_map;
    character_set m_afk_characters;
    character_id_set m_looting_characters;

    // For tracking when people engaged in PK can get AFK protection.
    time_map m_last_engaged_pk_time;

    // For tracking which spells are harmful.
    skill_id_set m_can_be_helpful_skills;
    skill_id_set m_harmful_skills;

#endif // USE_BIG_BROTHER
};
}

#endif // Header Protection
