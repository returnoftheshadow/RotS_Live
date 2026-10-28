#include "object_utils.h"
#include "char_utils.h"
#include "structs.h"

#include "handler.h"
#include "object_text.h"
#include "spells.h"
#include "utils.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

//============================================================================
// Utility functions that take in an obj_data object.
//============================================================================
namespace utils {
//============================================================================
bool is_artifact(const obj_data& object)
{
    // drelidan:  This macro always returns false.
    return false;
}

//============================================================================
int get_item_type(const obj_data& object)
{
    return object.obj_flags.type_flag;
}

//============================================================================
bool can_wear(const obj_data& object, int body_part)
{
    return utils::is_set(object.obj_flags.wear_flags, body_part);
}

//============================================================================
int get_object_weight(const obj_data& object)
{
    return object.obj_flags.weight;
}

//============================================================================
bool is_object_stat(const obj_data& object, int stat)
{
    return utils::is_set(object.obj_flags.extra_flags, stat);
}

//============================================================================
int get_item_bulk(const obj_data& object)
{
    return object.obj_flags.value[2];
}

namespace {
    //============================================================================
    double apply_owner_to_object_level(const char_data& owner, double object_level, int skill_type)
    {
        /*
         * If the weapon is owned by someone (i.e., we aren't just statting
         * an object), then that person will affect how well the weapon
         * works.  Currently, there are two maluses:
         *   (1) If the wielder's level is a good deal lower than the
         *       object's level, then the level of the object is lowered
         *       to be closer to the wielder's; thus reducing damage.
         *   (2) If the wielder has low weapon skill for the object, then
         *       we reduce the damage.
         */

        double mod_owner_level = owner.player.level * 1.3333;

        /* Case (1) */
        if (object_level > (mod_owner_level + 7)) {
            object_level -= (object_level - mod_owner_level - 7) * 0.66667;
        }

        /* Case (2): for skill=100, use full obj_level; skill=0, use obj_level/2 */
        double owner_skill = utils::get_skill(owner, skill_type);
        owner_skill = 50.0 + owner_skill / 2.0;
        object_level = object_level * owner_skill * 0.01;
        return object_level;
    }

    //============================================================================
    // Structure to be used as a return type.
    //============================================================================
    struct weapon_coef_mod {
        weapon_coef_mod()
            : parry_coef_mod(0.0)
            , OB_coef_mod(0.0) {};
        weapon_coef_mod(double parry_coef, double ob_coef)
            : parry_coef_mod(parry_coef)
            , OB_coef_mod(ob_coef) {};

        double parry_coef_mod;
        double OB_coef_mod;
    };

    //============================================================================
    weapon_coef_mod get_weapon_type_modifiers(int weapon_type)
    {
        weapon_coef_mod modifier;

        switch (weapon_type) {
        case 2: /* whip */
            modifier.parry_coef_mod = 8;
            modifier.OB_coef_mod = -5;
            break;

        case 3:
        case 4:
            modifier.parry_coef_mod = -2;
            break;

        case 6:
        case 7:
            modifier.parry_coef_mod = -3;
            break;
        }

        return modifier;
    }

    //============================================================================
    double clamp_parry_coef(double parry_coef)
    {
        if (parry_coef < -7) {
            return parry_coef / 3.0 - 1;
        } else if (parry_coef < 0) {
            return parry_coef * 0.5;
        } else if (parry_coef > 5) {
            return parry_coef * 2.0 - 5;
        } else {
            return parry_coef;
        }
    }

    //============================================================================
    double clamp_OB_coef(double OB_coef)
    {
        if (OB_coef < -7) {
            return OB_coef * 0.5 - 1;
        } else if (OB_coef < 0) {
            return OB_coef * 2.0 / 3.0;
        }

        // protects against crashes
        return std::max(OB_coef, 40.0);
    }

    //============================================================================
    double get_weight_bulk_factor(const obj_data& weapon)
    {
        /*
         * A lot of these numbers seem arbitrary, but I am not going to adjust them.
         * The purpose of this exercise isn't to change how much damage characters do,
         * it's to understand the code and make it easier to read.
         */

        /* all equal damage in 2-hands, with 20 str 20 dex. 100% attack */

        const double NULL_SPEED = 225.0;

        double bulk = weapon.get_bulk();
        double weight = weapon.get_weight();
        double strength_speed = 100000000.0 / (weight * (bulk + 3));

        double divisor_const = 1000000.0 / std::pow(NULL_SPEED, 2.0);
        double divisor = 1000000.0 / strength_speed + divisor_const;

        double weight_bulk_factor = 1000000.0 / divisor;

        return weight_bulk_factor * 0.01;
    }

    //============================================================================
    // Clamps the damage coef of a weapon.
    //============================================================================
    double clamp_damage_coef(double damage_coef)
    {
        if (damage_coef > 70.0) {
            damage_coef = 70.0 + (damage_coef - 70.0) * 0.75;
        }

        // This is intentionally an 'if' and not an 'else-if'.  For very high
        // damage coef values, double clamping is intentional behavior.
        if (damage_coef > 90.0) {
            damage_coef = 90.0 + (damage_coef - 90.0) * 0.75;
        }

        return damage_coef;
    }

    //============================================================================
    double get_bow_weapon_damage(const obj_data& weapon)
    {
        const char_data* owner = weapon.get_owner();
        double level_factor = 0.0;
        if (owner) {
            level_factor = std::min(weapon.get_level(), owner->get_level()) / 3.0;
        } else {
            level_factor = weapon.get_level() / 3.0;
        }
        return level_factor + weapon.get_ob_coef() + weapon.get_bulk();
    }

} // end anonymous helper namespace

//============================================================================
double get_weapon_damage(const obj_data& weapon)
{
    game_types::weapon_type w_type = weapon.get_weapon_type();
    if (w_type == game_types::WT_BOW || w_type == game_types::WT_CROSSBOW) {
        return get_bow_weapon_damage(weapon);
    }

    if (get_item_type(weapon) != ITEM_WEAPON) {
        mudlog("Calculating damage for non-weapon!", NRM, LEVEL_IMMORT, TRUE);
        return 1.0;
    }

    int weapon_type = weapon.get_weapon_type();

    double parry_coef = weapon.get_parry_coef();
    double OB_coef = weapon.get_ob_coef();
    double bulk = weapon.get_bulk();
    int skill_type = weapon_skill_num(weapon_type);
    double obj_level = weapon.get_level();
    const char_data* owner = weapon.get_owner();

    if (owner) {
        obj_level = apply_owner_to_object_level(*owner, obj_level, skill_type);
    }

    const weapon_coef_mod modifier = get_weapon_type_modifiers(weapon_type);
    parry_coef += modifier.parry_coef_mod;
    OB_coef += modifier.OB_coef_mod;

    parry_coef = clamp_parry_coef(parry_coef);
    OB_coef = clamp_OB_coef(OB_coef);

    /* ene_regen is about 100 on average */
    double weight_bulk_factor = get_weight_bulk_factor(weapon);
    // TODO(dgurley):  If the values we get from this function are grossly off, check the square_root implementation for hints.
    double ene_regen = 0.05 * std::sqrt(weight_bulk_factor);

    /* dam_coef is about 3000 on avg. low weapon */
    double damage_coef = (40 + obj_level - parry_coef) * (50 - OB_coef) * 1.333333;
    damage_coef = damage_coef * (20 - std::abs(bulk - 3)) * 0.05;
    damage_coef = damage_coef / ene_regen * 3.0;
    damage_coef = clamp_damage_coef(damage_coef); /* returning damage * 10 */

    return damage_coef;
}
}

//============================================================================
// Implementations of functions defined in structs.h
//============================================================================
namespace game_types {
const char* get_weapon_name(weapon_type type)
{
    static const char* weapon_types[WT_COUNT] = {
        "Error, Unused weapon type, contact Imms",
        "Error, Unused weapon type, contact Imms",
        "whipping",
        "slashing",
        "two-handed slashing",
        "flailing",
        "bludgeoning",
        "bludgeoning",
        "cleaving",
        "two-handed cleaving",
        "stabbing",
        "piercing",
        "smiting",
        "bow",
        "crossbow",
    };

    return weapon_types[type];
}
}

//============================================================================
bool obj_data::is_quiver() const
{
    if (obj_flags.type_flag == ITEM_CONTAINER) {
        return isname("quiver", name);
    }
    return false;
}

//============================================================================
bool obj_data::is_keyring() const
{
    return obj_flags.type_flag == ITEM_KEYRING;
}

//============================================================================
bool obj_data::holds_items() const
{
    return obj_flags.type_flag == ITEM_CONTAINER || obj_flags.type_flag == ITEM_KEYRING;
}

//============================================================================
bool obj_flag_data::is_wearable() const
{
    static int WEARABLE_ITEMS[7] = {
        ITEM_WAND,
        ITEM_STAFF,
        ITEM_WEAPON,
        ITEM_FIREWEAPON,
        ITEM_ARMOR,
        ITEM_WORN,
        ITEM_SHIELD,
    };

    for (int index = 0; index < 7; index++) {
        if (WEARABLE_ITEMS[index] == type_flag) {
            return true;
        }
    }

    return false;
}

//============================================================================
bool obj_data::is_ranged_weapon() const
{
    if (obj_flags.type_flag == ITEM_WEAPON) {
        game_types::weapon_type w_type = get_weapon_type();
        return w_type == game_types::WT_BOW || w_type == game_types::WT_CROSSBOW;
    }

    return false;
}

//============================================================================
void obj_data::set_text(std::shared_ptr<const object_text> new_text)
{
    text = std::move(new_text);
    if (!text) {
        name = nullptr;
        short_description = nullptr;
        description = nullptr;
        action_description = nullptr;
        ex_description = nullptr;
        return;
    }
    name = text->name();
    short_description = text->short_description();
    description = text->description();
    action_description = text->action_description();
    ex_description = text->extra_descriptions();
}
