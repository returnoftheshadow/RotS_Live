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

#include "caster_snapshot.h" /* For the snapshot form of saves_poison() */
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
#define SKILL_BAREHANDED 0
#define SKILL_SLASH 1
#define SKILL_CONCUSSION 2
#define SKILL_WHIP 3
#define SKILL_PIERCE 4
#define SKILL_SPEARS 5
#define SKILL_AXE 6
#define SKILL_NATURAL_ATTACK 7
#define SKILL_SWIM 8
#define SKILL_TWOHANDED 9
#define SKILL_WEAPONS 10
#define SKILL_PARRY 11
#define SKILL_KICK 12
#define SKILL_BASH 13
#define SKILL_RESCUE 14
#define SKILL_BERSERK 15
#define SKILL_EXTRA_DAMAGE 16
#define SKILL_BLOCK 17
#define SKILL_SWING 18
#define SKILL_DEFEND 131

/* Ranger skills */
#define SKILL_DODGE 21
#define SKILL_ATTACK 22
#define SKILL_SNEAK 23
#define SKILL_HIDE 24
#define SKILL_AMBUSH 25
#define SKILL_TRACK 26
#define SKILL_PICK_LOCK 27
#define SKILL_SEARCH 28
#define SKILL_ANIMALS 29
#define SKILL_GATHER_FOOD 30
#define SKILL_STEALTH 31
#define SKILL_AWARENESS 32
#define SKILL_RIDE 33
#define SKILL_ACCURACY 34
#define SKILL_TAME 35
#define SKILL_CALM 36
#define SKILL_WHISTLE 37
#define SKILL_STALK 38
#define SKILL_TRAVELLING 39
#define SKILL_RIPOSTE 20
#define SKILL_ARCHERY 61

/* Common orc recruit skill */
#define SKILL_RECRUIT 40
#define SKILL_LEADERSHIP 19

/* Mystic Spells */
#define SPELL_DETECT_HIDDEN 41
#define SPELL_ARMOR 42
#define SPELL_POISON 43
#define SPELL_RESIST_POISON 44
#define SPELL_CURING 45
#define SPELL_RESTLESSNESS 46
#define SPELL_RESIST_MAGIC 47
#define SPELL_SLOW_DIGESTION 48
#define SPELL_DISPEL_REGENERATION 49
#define SPELL_INSIGHT 50
#define SPELL_PRAGMATISM 51
#define SPELL_HAZE 52
#define SPELL_FEAR 53
#define SPELL_DIVINATION 54
#define SPELL_SANCTUARY 56
#define SPELL_VITALITY 57
#define SPELL_TERROR 58
#define SPELL_ENCHANT_WEAPON 60
#define SPELL_HALLUCINATE 63
#define SPELL_REGENERATION 64
#define SPELL_GUARDIAN 65
#define SPELL_INFRAVISION 66
#define SPELL_CURSE 67
#define SPELL_REVIVE 68
#define SPELL_DETECT_MAGIC 69
#define SPELL_SHIFT 70 // Needs to be removed.
#define SPELL_DEATH_WARD 83
#define SPELL_MIND_BLOCK 86
#define SPELL_REMOVE_POISON 87
#define SPELL_PROTECTION 89
#define SPELL_CONFUSE 111

/* Mage Spells */
#define SPELL_SUMMON 62
#define SPELL_MAGIC_MISSILE 71
#define SPELL_REVEAL_LIFE 72
#define SPELL_LOCATE_LIVING 73
#define SPELL_CURE_SELF 74
#define SPELL_CHILL_RAY 75
#define SPELL_BLINK 76
#define SPELL_FREEZE 77 // Needs to be removed.
#define SPELL_LIGHTNING_BOLT 78
#define SPELL_VITALIZE_SELF 79
#define SPELL_FLASH 80
#define SPELL_EARTHQUAKE 81
#define SPELL_CREATE_LIGHT 82
#define SPELL_DARK_BOLT 84
#define SPELL_MIST_OF_BAAZUNGA 85 // Needs to be removed.
#define SPELL_BEACON 88
#define SPELL_BLAZE 90 // Needs to be removed
#define SPELL_FIREBOLT 91
#define SPELL_RELOCATE 92
#define SPELL_CONE_OF_COLD 93
#define SPELL_IDENTIFY 94
#define SPELL_FIREBALL 96
#define SPELL_SEARING_DARKNESS 98
#define SPELL_LIGHTNING_STRIKE 99
#define SPELL_SHIELD 108
#define SPELL_DETECT_EVIL 109
#define SPELL_EXPOSE_ELEMENTS 112

/* Uruk-Lhuth Race Specific Spells */
#define SPELL_WORD_OF_PAIN 100
#define SPELL_WORD_OF_SIGHT 101
#define SPELL_WORD_OF_AGONY 102
#define SPELL_SHOUT_OF_PAIN 103
#define SPELL_WORD_OF_SHOCK 104
#define SPELL_SPEAR_OF_DARKNESS 105
#define SPELL_LEACH 106
#define SPELL_BLACK_ARROW 107

/* Beornings Race Specific Skills */
#define SKILL_REND 55
#define SKILL_BITE 113
#define SKILL_SWIPE 114
#define SKILL_MAUL 115

/* Haradrim Race Specific Skills */
#define SKILL_MARK 124
#define SKILL_BLINDING 110
#define SKILL_BEND_TIME 95
#define SKILL_WINDBLAST 128
#define SPELL_FAME_WAR 129

/* Olog-Hai Race Specific Skills */
#define SKILL_SMASH 152
#define SKILL_FRENZY 153
#define SKILL_STOMP 154
#define SKILL_BLOODTHIRST 155
#define SKILL_CLEAVE 156
#define SKILL_OVERRUN 157
#define SPELL_MASS_REGENERATION 158
#define SPELL_MASS_VITALITY 159
#define SPELL_MASS_INSIGHT 160

#define SPELL_ASPHYXIATION 116
#define SPELL_ARDA 117
#define SPELL_ACTIVITY 118
#define SPELL_RAGE 119
#define SPELL_ANGER 120
/* These are reserved for languages do not used them...
        #define LANG_ANIMAL      121
        #define LANG_HUMAN       122
        #define LANG_ORC         123
*/

/* Any skills and/or spells free should be listed here */

#define SKILL_UNUSED2 97
#define SKILL_UNUSED3 59
#define SKILL_UNUSED5 126

/* Don't remove this because it's use to remove spells */
#define SPELL_NONE 127
/* MAX_SKILL is defined at 128 and we can't exceed this without changing the playerfiles */

/* 130-150 are reserved for weapon types.  200 is suffering */
#define SKILL_TRAP 151

#define SPELL_FIREBALL2 201
#define SPELL_DRAGONSBREATH 202
#define SPELL_BURN 203

#define TYPE_UNDEFINED -1
#define SPELL_RESERVED_DBC 0 /* SKILL NUMBER ZERO */

/* END OF SKILL RESERVED "NO TOUCH" NUMBERS */

/*
 * TYPE_HIT and TYPE_CRUSH must be the first and last weapon
 * types, else fight.cc's IS_PHYSICAL macro must be changed to
 * reflect this.  Note that IS_PHYSICAL is not ubiquitously
 * used throughout the code, so changing it will not be suffi-
 * cient to ensure that the rest of the code works properly.
 */
#define TYPE_HIT 130
#define TYPE_BLUDGEON 131
#define TYPE_PIERCE 132
#define TYPE_SLASH 133
#define TYPE_STAB 134
#define TYPE_WHIP 135 /* EXAMPLE */
#define TYPE_SPEARS 136
#define TYPE_CLAW 137 /* NO MESSAGES WRITTEN YET! */
#define TYPE_BITE 138 /* NO MESSAGES WRITTEN YET! */
#define TYPE_STING 139 /* NO MESSAGES WRITTEN YET! */
#define TYPE_CLEAVE 140
#define TYPE_FLAIL 141
#define TYPE_SMITE 142
#define TYPE_CRUSH 143

#define TYPE_SUFFERING 200

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

#define MAX_TYPES 100

#define MAX_SPL_LIST 127

/*
 * For bitvector defines for learn_type.
 *
 * NOTE: It looks like LEARN_LEVEL and DYNAMIC_TIME are deprecated.
 */
#define LEARN_COOF 0x01 /* Learning speed proportional to class_coof */
#define LEARN_LEVEL 0x02 /* Learning speed depends on spell lvl and coof */
#define DYNAMIC_TIME 0x10 /* Duration is decreased with level */
#define REDUCED_MANA 0x20 /* Mana when casting is reduced by 5 */
#define LEARN_SPEC 0x40 /* May learn only if specialized in it */

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

#define SPELL_TYPE_SPELL 0
#define SPELL_TYPE_POTION 1
#define SPELL_TYPE_WAND 2
#define SPELL_TYPE_STAFF 3
#define SPELL_TYPE_SCROLL 4
#define SPELL_TYPE_ANTI 5 /* The spell is asked to self-destruct */

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

bool is_strong_enough_to_tame(struct char_data* tamer, struct char_data* animal, bool include_current_followers);

int get_mage_caster_level(const char_data* caster);
int get_mystic_caster_level(const char_data* caster);

// saves_poison()'s two forms (TASK-021). The live struct char_data* form is a
// one-line forwarder onto the caster_snapshot form, which owns the body: a
// poison that ticks after its caster is gone runs the identical formula from
// the cast-time copy instead of touching a possibly-freed character.
char saves_poison(struct char_data* victim, struct char_data* caster);
char saves_poison(struct char_data* victim, const caster_snapshot& caster);

#endif /* SPELLS_H */
