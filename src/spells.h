/* ************************************************************************
 *   File: spells.h                                      Part of CircleMUD *
 *  Usage: header file: constants and fn prototypes for spell system       *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef SPELLS_H
#define SPELLS_H

#include "platdef.h" /* For sh_int, ush_int, byte, etc. */
#include "structs.h" /* For the MAX_SKILLS macro */
#include <algorithm>

/*
 * Spell ranges are:
 *   0 to 20: warrior skills,
 *  21 to 40: ranger skills,
 *  40 to 60: mage spells,
 *  60 to 80: cleric powers.
 *
 * The above ranges are only general ideas, and shouldn't be
 * taken to be correct.  Random skills/spells/powers are distrib-
 * uted throughout the ranges.
 *
 * The first 10 skills (which are warrior skills) are reserved
 * for weapon skills only.  Although swim has been stuck in slot
 * 8.
 *
 * Weapon types are 130 to 140.
 */

/* Warrior Skills */
inline constexpr int SKILL_BAREHANDED = 0;
inline constexpr int SKILL_SLASH = 1;
inline constexpr int SKILL_CONCUSSION = 2;
inline constexpr int SKILL_WHIP = 3;
inline constexpr int SKILL_PIERCE = 4;
inline constexpr int SKILL_SPEARS = 5;
inline constexpr int SKILL_AXE = 6;
inline constexpr int SKILL_NATURAL_ATTACK = 7;
inline constexpr int SKILL_SWIM = 8;
inline constexpr int SKILL_TWOHANDED = 9;
inline constexpr int SKILL_WEAPONS = 10;
inline constexpr int SKILL_PARRY = 11;
inline constexpr int SKILL_KICK = 12;
inline constexpr int SKILL_BASH = 13;
inline constexpr int SKILL_RESCUE = 14;
inline constexpr int SKILL_BERSERK = 15;
inline constexpr int SKILL_EXTRA_DAMAGE = 16;
inline constexpr int SKILL_BLOCK = 17;
inline constexpr int SKILL_SWING = 18;
inline constexpr int SKILL_DEFEND = 131;

/* Ranger skills */
inline constexpr int SKILL_DODGE = 21;
inline constexpr int SKILL_ATTACK = 22;
inline constexpr int SKILL_SNEAK = 23;
inline constexpr int SKILL_HIDE = 24;
inline constexpr int SKILL_AMBUSH = 25;
inline constexpr int SKILL_TRACK = 26;
inline constexpr int SKILL_PICK_LOCK = 27;
inline constexpr int SKILL_SEARCH = 28;
inline constexpr int SKILL_ANIMALS = 29;
inline constexpr int SKILL_GATHER_FOOD = 30;
inline constexpr int SKILL_STEALTH = 31;
inline constexpr int SKILL_AWARENESS = 32;
inline constexpr int SKILL_RIDE = 33;
inline constexpr int SKILL_ACCURACY = 34;
inline constexpr int SKILL_TAME = 35;
inline constexpr int SKILL_CALM = 36;
inline constexpr int SKILL_WHISTLE = 37;
inline constexpr int SKILL_STALK = 38;
inline constexpr int SKILL_TRAVELLING = 39;
inline constexpr int SKILL_RIPOSTE = 20;
inline constexpr int SKILL_ARCHERY = 61;

/* Common orc recruit skill */
inline constexpr int SKILL_RECRUIT = 40;
inline constexpr int SKILL_LEADERSHIP = 19;

/* Mystic Spells */
inline constexpr int SPELL_DETECT_HIDDEN = 41;
inline constexpr int SPELL_ARMOR = 42;
inline constexpr int SPELL_POISON = 43;
inline constexpr int SPELL_RESIST_POISON = 44;
inline constexpr int SPELL_CURING = 45;
inline constexpr int SPELL_RESTLESSNESS = 46;
inline constexpr int SPELL_RESIST_MAGIC = 47;
inline constexpr int SPELL_SLOW_DIGESTION = 48;
inline constexpr int SPELL_DISPEL_REGENERATION = 49;
inline constexpr int SPELL_INSIGHT = 50;
inline constexpr int SPELL_PRAGMATISM = 51;
inline constexpr int SPELL_HAZE = 52;
inline constexpr int SPELL_FEAR = 53;
inline constexpr int SPELL_DIVINATION = 54;
inline constexpr int SPELL_SANCTUARY = 56;
inline constexpr int SPELL_VITALITY = 57;
inline constexpr int SPELL_TERROR = 58;
inline constexpr int SPELL_ENCHANT_WEAPON = 60;
inline constexpr int SPELL_HALLUCINATE = 63;
inline constexpr int SPELL_REGENERATION = 64;
inline constexpr int SPELL_GUARDIAN = 65;
inline constexpr int SPELL_INFRAVISION = 66;
inline constexpr int SPELL_CURSE = 67;
inline constexpr int SPELL_REVIVE = 68;
inline constexpr int SPELL_DETECT_MAGIC = 69;
inline constexpr int SPELL_SHIFT = 70; // Needs to be removed.
inline constexpr int SPELL_DEATH_WARD = 83;
inline constexpr int SPELL_MIND_BLOCK = 86;
inline constexpr int SPELL_REMOVE_POISON = 87;
inline constexpr int SPELL_PROTECTION = 89;
inline constexpr int SPELL_CONFUSE = 111;
inline constexpr int SPELL_RESIST_FIRE = 161;
inline constexpr int SPELL_RESIST_COLD = 162;
inline constexpr int SPELL_RESIST_LIGHT = 163;
inline constexpr int SPELL_RESIST_ILLUSION = 164;
inline constexpr int SPELL_RESIST_PHYSICAL = 165;
inline constexpr int SPELL_RESIST_DARK = 166;

/* Mage Spells */
inline constexpr int SPELL_SUMMON = 62;
inline constexpr int SPELL_MAGIC_MISSILE = 71;
inline constexpr int SPELL_REVEAL_LIFE = 72;
inline constexpr int SPELL_LOCATE_LIVING = 73;
inline constexpr int SPELL_CURE_SELF = 74;
inline constexpr int SPELL_CHILL_RAY = 75;
inline constexpr int SPELL_BLINK = 76;
inline constexpr int SPELL_FREEZE = 77; // Needs to be removed.
inline constexpr int SPELL_LIGHTNING_BOLT = 78;
inline constexpr int SPELL_VITALIZE_SELF = 79;
inline constexpr int SPELL_FLASH = 80;
inline constexpr int SPELL_EARTHQUAKE = 81;
inline constexpr int SPELL_CREATE_LIGHT = 82;
inline constexpr int SPELL_DARK_BOLT = 84;
inline constexpr int SPELL_MIST_OF_BAAZUNGA = 85; // Needs to be removed.
inline constexpr int SPELL_BEACON = 88;
inline constexpr int SPELL_BLAZE = 90; // Needs to be removed
inline constexpr int SPELL_FIREBOLT = 91;
inline constexpr int SPELL_RELOCATE = 92;
inline constexpr int SPELL_CONE_OF_COLD = 93;
inline constexpr int SPELL_IDENTIFY = 94;
inline constexpr int SPELL_FIREBALL = 96;
inline constexpr int SPELL_SEARING_DARKNESS = 98;
inline constexpr int SPELL_LIGHTNING_STRIKE = 99;
inline constexpr int SPELL_SHIELD = 108;
inline constexpr int SPELL_DETECT_EVIL = 109;
inline constexpr int SPELL_EXPOSE_ELEMENTS = 112;

/* Uruk-Lhuth Race Specific Spells */
inline constexpr int SPELL_WORD_OF_PAIN = 100;
inline constexpr int SPELL_WORD_OF_SIGHT = 101;
inline constexpr int SPELL_WORD_OF_AGONY = 102;
inline constexpr int SPELL_SHOUT_OF_PAIN = 103;
inline constexpr int SPELL_WORD_OF_SHOCK = 104;
inline constexpr int SPELL_SPEAR_OF_DARKNESS = 105;
inline constexpr int SPELL_LEACH = 106;
inline constexpr int SPELL_BLACK_ARROW = 107;

/* Beornings Race Specific Skills */
inline constexpr int SKILL_REND = 55;
inline constexpr int SKILL_BITE = 113;
inline constexpr int SKILL_SWIPE = 114;
inline constexpr int SKILL_MAUL = 115;

/* Haradrim Race Specific Skills */
inline constexpr int SKILL_MARK = 124;
inline constexpr int SKILL_BLINDING = 110;
inline constexpr int SKILL_BEND_TIME = 95;
inline constexpr int SKILL_WINDBLAST = 128;
inline constexpr int SPELL_FAME_WAR = 129;

/* Olog-Hai Race Specific Skills */
inline constexpr int SKILL_SMASH = 152;
inline constexpr int SKILL_FRENZY = 153;
inline constexpr int SKILL_STOMP = 154;
inline constexpr int SKILL_BLOODTHIRST = 155;
inline constexpr int SKILL_CLEAVE = 156;
inline constexpr int SKILL_OVERRUN = 157;
inline constexpr int SPELL_MASS_REGENERATION = 158;
inline constexpr int SPELL_MASS_VITALITY = 159;
inline constexpr int SPELL_MASS_INSIGHT = 160;

inline constexpr int SPELL_ASPHYXIATION = 116;
inline constexpr int SPELL_ARDA = 117;
inline constexpr int SPELL_ACTIVITY = 118;
inline constexpr int SPELL_RAGE = 119;
inline constexpr int SPELL_ANGER = 120;
/* These are reserved for languages do not used them...
        #define LANG_ANIMAL      121
        #define LANG_HUMAN       122
        #define LANG_ORC         123
*/

/* Any skills and/or spells free should be listed here */

inline constexpr int SKILL_UNUSED2 = 97;
inline constexpr int SKILL_UNUSED3 = 59;
inline constexpr int SKILL_UNUSED5 = 126;

/* Don't remove this because it's use to remove spells */
inline constexpr int SPELL_NONE = 127;
/* MAX_SKILL is defined at 128 and we can't exceed this without changing the playerfiles */

/* 130-150 are reserved for weapon types.  200 is suffering */
inline constexpr int SKILL_TRAP = 151;

inline constexpr int SPELL_FIREBALL2 = 201;
inline constexpr int SPELL_DRAGONSBREATH = 202;
inline constexpr int SPELL_BURN = 203;

inline constexpr int TYPE_UNDEFINED = -1;
inline constexpr int SPELL_RESERVED_DBC = 0; /* SKILL NUMBER ZERO */

/* END OF SKILL RESERVED "NO TOUCH" NUMBERS */

/*
 * TYPE_HIT and TYPE_CRUSH must be the first and last weapon
 * types, else fight.cc's IS_PHYSICAL macro must be changed to
 * reflect this.  Note that IS_PHYSICAL is not ubiquitously
 * used throughout the code, so changing it will not be suffi-
 * cient to ensure that the rest of the code works properly.
 */
inline constexpr int TYPE_HIT = 130;
inline constexpr int TYPE_BLUDGEON = 131;
inline constexpr int TYPE_PIERCE = 132;
inline constexpr int TYPE_SLASH = 133;
inline constexpr int TYPE_STAB = 134;
inline constexpr int TYPE_WHIP = 135; /* EXAMPLE */
inline constexpr int TYPE_SPEARS = 136;
inline constexpr int TYPE_CLAW = 137; /* NO MESSAGES WRITTEN YET! */
inline constexpr int TYPE_BITE = 138; /* NO MESSAGES WRITTEN YET! */
inline constexpr int TYPE_STING = 139; /* NO MESSAGES WRITTEN YET! */
inline constexpr int TYPE_CLEAVE = 140;
inline constexpr int TYPE_FLAIL = 141;
inline constexpr int TYPE_SMITE = 142;
inline constexpr int TYPE_CRUSH = 143;

inline constexpr int TYPE_SUFFERING = 200;

/* More anything but spells and weapontypes can be insterted here! */
inline int weapon_skill_num(int type)
{
    switch (type) {
    case 1:
        return SKILL_WEAPONS;
    case 2:
    case 5:
    case 13:
        return SKILL_WHIP;
    case 3:
    case 4:
        return SKILL_SLASH;
    case 6:
    case 7:
    case 12:
    case 14:
        return SKILL_CONCUSSION;
    case 8:
    case 9:
        return SKILL_AXE;
    case 10:
        return SKILL_SPEARS;
    case 11:
        return SKILL_PIERCE;
    default:
        return SKILL_BAREHANDED;
    };
}

//============================================================================
inline int weapon_skill_num(game_types::weapon_type weapon_type)
{
    switch (weapon_type) {
    case game_types::WT_UNUSED_1:
    case game_types::WT_UNUSED_2:
        return SKILL_WEAPONS;
    case game_types::WT_WHIPPING:
    case game_types::WT_FLAILING:
        return SKILL_WHIP;
        break;
    case game_types::WT_SLASHING:
    case game_types::WT_SLASHING_TWO:
        return SKILL_SLASH;
        break;
    case game_types::WT_BLUDGEONING:
    case game_types::WT_BLUDGEONING_TWO:
    case game_types::WT_SMITING:
        return SKILL_CONCUSSION;
        break;
    case game_types::WT_CLEAVING:
    case game_types::WT_CLEAVING_TWO:
        return SKILL_AXE;
        break;
    case game_types::WT_STABBING:
        return SKILL_SPEARS;
        break;
    case game_types::WT_PIERCING:
        return SKILL_PIERCE;
        break;
    case game_types::WT_COUNT:
        return SKILL_BAREHANDED;
        break;
    case game_types::WT_CROSSBOW:
    case game_types::WT_BOW:
        return SKILL_ARCHERY;
        break;
    default:
        return SKILL_BAREHANDED;
        break;
    }
}

inline constexpr int MAX_TYPES = 100;

inline constexpr int MAX_SPL_LIST = 127;

/*
 * For bitvector defines for learn_type.
 *
 * NOTE: It looks like LEARN_LEVEL and DYNAMIC_TIME are deprecated.
 */
inline constexpr int LEARN_COOF = 0x01; /* Learning speed proportional to class_coof */
inline constexpr int LEARN_LEVEL = 0x02; /* Learning speed depends on spell lvl and coof */
inline constexpr int DYNAMIC_TIME = 0x10; /* Duration is decreased with level */
inline constexpr int REDUCED_MANA = 0x20; /* Mana when casting is reduced by 5 */
inline constexpr int LEARN_SPEC = 0x40; /* May learn only if specialized in it */

#define SPELL_LEVEL(ch, sn) (skills[(sn)].level)

#define USE_MANA(ch, sn) \
    std::max(skills[sn].min_usesmana, 120 / (3 + std::max(-1, GET_PROF_LEVEL(PROF_MAGE, ch) - SPELL_LEVEL(ch, sn))) - (IS_SET(skills[sn].learn_type, REDUCED_MANA) ? 5 : 0))

#define USE_SPIRIT(ch, sn) (skills[sn].min_usesmana)

#define CASTING_TIME(ch, sn) \
    ((skills[sn].beats * 30) / (30 + GET_PROF_LEVEL((int)skills[sn].type, ch)))

struct char_data;
struct obj_data;

/*
 * For the 'target' member, possible targets are:
 *   bit 0: IGNORE TARGET
 *   bit 1: PC/NPC in room
 *   bit 2: PC/NPC in world
 *   bit 3: Object held
 *   bit 4: Object in inventory
 *   bit 5: Object in room
 *   bit 6: Object in world
 *   bit 7: If fighting, and no argument, select tar_char as self
 *   bit 8: If fighting, and no argument, select tar_char as victim (fighting)
 *   bit 9: If no argument, select self, if argument check that it IS self.
 *
 * NOTE: Many more target types exist.  See the skill array in
 * consts.cc for some commentary.
 */
struct skill_data {
    char name[50];
    char type;
    char level;
    void (*spell_pointer)(char_data* caster, char* arg,
        int type, char_data* tar_ch,
        obj_data* tar_obj, int digit, int is_object);
    byte minimum_position; /* Position for caster */
    int min_usesmana; /* Amount of mana used by a spell */
    byte beats; /* Heartbeats until ready for next */
    int targets; /* See above for use with TAR_XXX  */
    int learn_diff; /* difficulty */
    char learn_type; /* If the skill is spec only set to 65 otherwise 1  */
    byte is_fast; /* non-zero if fast-updating skill */
    char skill_spec; /* spell/skill group, specialization */
    char resist; /* RESIST_* this skill's damage is resisted as */
};

struct skill_teach_data {
    char* list_message;
    char* reject_message;
    char* practice_message;
    char* practice_msg_to_char;
    char* limit_message;
    char* learned_message;
    byte knowledge[MAX_SKILLS];
};

inline constexpr int SPELL_TYPE_SPELL = 0;
inline constexpr int SPELL_TYPE_POTION = 1;
inline constexpr int SPELL_TYPE_WAND = 2;
inline constexpr int SPELL_TYPE_STAFF = 3;
inline constexpr int SPELL_TYPE_SCROLL = 4;
inline constexpr int SPELL_TYPE_ANTI = 5; /* The spell is asked to self-destruct */

/* Attack types with grammar */
struct attack_hit_type {
    char* singular;
    char* plural;
};

void recalc_skills(struct char_data*);

#define ASPELL(castname)                             \
    void                                             \
    castname(char_data* caster, char* arg, int type, \
        char_data* victim, obj_data* obj, int digit, \
        int is_object)

/* For a spell that works on a character: true, after logging it and telling <caster> that nothing
 * seems to happen, when <victim> is null. Only a staff, or a wand pointed at an object, gives a
 * spell nobody to work on, and only a builder's mistaken item carries such a spell. */
bool spell_lacks_victim(char_data* caster, const char_data* victim, int spell_number);

/* True for a spell whose item form (APPLY_SPELL) gives the wearer a lasting affect as the item
 * goes on, takes it as the item comes off, and does nothing else: the one kind of item spell a
 * quiet gear change (gear_change::quiet) still casts. Death ward is the exception: taking its
 * item off removes insight and leaves death ward, and putting it on removes insight too. A spell
 * whose item form starts or stops working that way is added here or taken out, or a login stops
 * giving it or casts its harm. */
bool item_spell_is_worn_affect(int spell_number);

/* Mage spell prototypes */
ASPELL(spell_blink);
ASPELL(spell_magic_missile);
ASPELL(spell_chill_ray);
ASPELL(spell_lightning_strike);
ASPELL(spell_fireball);
ASPELL(spell_identify);
ASPELL(spell_cone_of_cold);
ASPELL(spell_relocate);
ASPELL(spell_firebolt);
ASPELL(spell_flash);
ASPELL(spell_earthquake);
ASPELL(spell_vitalize_self);
ASPELL(spell_lightning_bolt);
ASPELL(spell_dark_bolt);
ASPELL(spell_freeze);
ASPELL(spell_blink);
ASPELL(spell_chill_ray);
ASPELL(spell_cure_self);
ASPELL(spell_reveal_life);
ASPELL(spell_locate_living);
ASPELL(spell_create_light);
ASPELL(spell_summon);
ASPELL(spell_identify);
ASPELL(spell_summon);
ASPELL(spell_blaze);
ASPELL(spell_beacon);
ASPELL(spell_shield);
ASPELL(spell_searing_darkness);

/* Lhuth spell prototypes */
ASPELL(spell_word_of_pain);
ASPELL(spell_word_of_sight);
ASPELL(spell_word_of_agony);
ASPELL(spell_word_of_shock);
ASPELL(spell_shout_of_pain);
ASPELL(spell_spear_of_darkness);
ASPELL(spell_leach);
ASPELL(spell_black_arrow);

/* Mystic spell prototypes */
ASPELL(spell_haze);
ASPELL(spell_detect_hidden);
ASPELL(spell_detect_evil);
ASPELL(spell_detect_magic);
ASPELL(spell_evasion);
ASPELL(spell_curing);
ASPELL(spell_restlessness);
ASPELL(spell_resist_magic);
ASPELL(spell_slow_digestion);
ASPELL(spell_dispel_regeneration);
ASPELL(spell_fear);
ASPELL(spell_divination);
ASPELL(spell_sanctuary);
ASPELL(spell_vitality);
ASPELL(spell_terror);
ASPELL(spell_hallucinate);
ASPELL(spell_regeneration);
ASPELL(spell_guardian);
ASPELL(spell_infravision);
ASPELL(spell_curse);
ASPELL(spell_enchant_weapon);
ASPELL(spell_poison);
ASPELL(spell_remove_poison);
ASPELL(spell_sanctuary);
ASPELL(spell_shift);
ASPELL(spell_revive);
ASPELL(spell_insight);
ASPELL(spell_pragmatism);
ASPELL(spell_death_ward);
ASPELL(spell_mist_of_baazunga);
ASPELL(spell_protection);
ASPELL(spell_mind_block);
ASPELL(spell_resist_poison);
ASPELL(spell_attune);
ASPELL(spell_confuse);
ASPELL(spell_expose_elements);

ASPELL(spell_mass_regeneration);
ASPELL(spell_mass_vitality);
ASPELL(spell_mass_insight);

ASPELL(spell_resist_fire);
ASPELL(spell_resist_cold);
ASPELL(spell_resist_light);
ASPELL(spell_resist_illusion);
ASPELL(spell_resist_physical);
ASPELL(spell_resist_dark);

bool is_strong_enough_to_tame(const struct char_data* tamer, const struct char_data* animal, bool include_current_followers);

int get_mage_caster_level(const char_data* caster);
int get_mystic_caster_level(const char_data* caster);

#endif /* SPELLS_H */
