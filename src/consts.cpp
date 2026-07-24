/**************************************************************************
 *   File: constants.c                                   Part of CircleMUD *
 *  Usage: Numeric and string contants used by the MUD                     *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 19 0, 1991.               *
 ************************************************************************ */
#define CONSTANTSMARK
#include <stdio.h>

#include "db.h"
#include "interpre.h"
#include "spells.h"
#include "structs.h"

char circlemud_version[] = { "Arda: The Fourth Age, version 1.5.7\n\r" };

// const
char* prof_abbrevs[] = { "--", "Mu", "Cl", "Ra", "Wa" };

// const
char* spell_wear_off_msg[] = {
    "RESERVED DB.C",
    "You feel less protected.", //* 1 *
    "!Teleport!",
    "You feel less righteous.",
    "You feel a cloak of blindness dissolve.",
    "!Burning Hands!", //* 5 *
    "!Call Lightning",
    "You feel more self-confident.",
    "!Chill Touch!",
    "!Clone!",
    "!Color Spray!", //* 10 *
    "!Control Weather!",
    "!Create Food!",
    "!Create Water!",
    "!Cure Blind!",
    "!Cure Critic!",
    "!Cure Light!",
    "You feel better.",
    "You sense the red in your vision disappear.",
    "The detect invisible wears off.",
    "The detect magic wears off.", //* 20 *
    "The detect poison wears off.",
    "!Dispel Evil!",
    "!Earthquake!",
    "!Enchant Weapon!",
    "!Energy Drain!",
    "!Fireball!",
    "!Harm!",
    "!Heal!",
    "You feel yourself exposed.",
    "The effects of the herbs have worn off.", //* 30 *
    "!Locate object!",
    "!Magic Missile!",
    "You feel less sick1.",
    "You feel less protected.",
    "!Remove Curse!",
    "The white aura around your body fades.",
    "!Shocking Grasp!",
    "You feel less tired.",
    "You feel weaker.",
    "!Summon!", //* 40 *
    "You become less aware of your surroundings.",
    "You feel less protected.",
    "You feel less sick.",
    "", // resist poison
    "Your regeneration slowed.",
    "You feel less energetic.",
    "You feel more vulnerable to magic.",
    "Your stomach is rumbling loudly.",
    "!Pick Lock!",
    "The world seems dull again.", //* 50 *
    "The world regains some romance again.",
    "Your surroundings stop spinning.",
    "You overcome your fear.",
    "You feel less protected.",
    "!Remove Curse!",
    "The white aura around your body fades.",
    "You feel heavier again.",
    "You feel less tired.",
    "You feel weaker.",
    "!Summon!", //* 60 *
    "!Locate object!",
    "!Magic Missile!",
    "Your surroundings come back into focus.",
    "Your regeneration slows again.",
    "!Remove Curse!",
    "Your eyesight becomes duller.",
    "!Shocking Grasp!",
    "You feel less tired.",
    "Your vision returns to normal.",
    "!Summon!", //* 70 *
    "!Locate object!",
    "!Magic Missile!",
    "You feel less sick4.",
    "You feel less protected.",
    "!Remove Curse!",
    "The white aura around your body fades.",
    "!Shocking Grasp!",
    "You feel less tired.",
    "You feel weaker.",
    "!Summon!", //* 80 *
    "!Locate object!",
    "!Magic Missile!",
    "The ward around you dissolves.",
    "You feel less protected.",
    "The dark, swirling mists disperse into the wind.",
    "Your mind feels more vulnerable to attack.",
    "Remove poison",
    "Your beacon dissolves.",
    "Your resistance wears off.",
    "!Summon!", //* 90 *
    "!Locate object!",
    "!Magic Missile!",
    "You feel less sick6.",
    "You feel less protected.",
    "You feel drained as the power of the land leaves your body.", //* 95 *
    "The white aura around your body fades.",
    "!Shocking Grasp!",
    "You feel less tired.",
    "You feel weaker.",
    "!Summon!", //*100 *
    "You feel less sensitive to magic.",
    "!Magic Missile!",
    "You feel less sick.",
    "You feel less protected.",
    "!Remove Curse!",
    "The white aura around your body fades.",
    "!Shocking Grasp!",
    "Your magical shield dissolves.",
    "You feel weaker.",
    "Your eyesight returns as if a veil has been lifted.", //* 110 *
    "You feel less confused.",
    "!Magic Missile!",
    "You feel less sick8.",
    "You feel less protected.",
    "You feel the affects of maul leave your body.",
    "You can breathe more easily",
    "The Power of Arda no longer weakens you.",
    "You feel less tired.",
    "You feel weaker.",
    "", //*120 *
    "!Locate object!",
    "!Magic Missile!",
    "You feel less sick9.",
    "The festering wound on your side closes up completely.",
    "!Remove Curse!",
    "The white aura around your body fades.",
    "!Shocking Grasp!",
    "!Searing Darkness!",
    "You feel the empowerment of war leave your body!",
    "", // * 130 *
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "", // * 140 *
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "", // * 150 *
    "",
    "",
    "The rage inside you slows down, allowing you to think clearly again.",
    "",
    "",
    "",
    "",
    "",
    "",
    "", // * 160 *
};

char* room_bits_message[32] = { "Dark mist permeats the air.",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /* 10 */ "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /* 20 */ "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /* 30 */ "",
    "" };

char* room_spell_message[MAX_SKILLS] = {
    "",
    /* 1*/ "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /* 11*/ "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /* 21*/ "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /* 31*/ "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /* 41*/ "",
    "",
    "There is an unpleasant smell to this place.",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /* 51*/ "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /* 61*/ "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /* 71*/ "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /* 81*/ "",
    "",
    "",
    "",
    "Dark, swirling mists blacken out the skies.",
    "",
    "",
    "",
    "",
    "The whole place is ablaze with fire, the flames reaching to you.",
    /* 91*/ "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /*101*/ "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /*111*/ "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
    /*121*/ "",
    "",
    "",
    "",
    "",
    "",
    ""

};

/*
 * The format used below is:
 * name, type, level, func_pointer,
 * min_pos, min_mana, time (beats), targets, learn_diff, \
 *   learn_type, is_fast, skill_spec
 *
 * Flags for valid targets are:
 * It will default to the last target in 1st line (I don't know what
 * this means, yet).
 *
 *     1   Ignore
 *     2   Anyone in the room
 *     4   Anyone in the world
 *     8   Self (default)
 *    16   Victim (fight, default)
 *    32   Only self
 *    64   Never self
 *   128   Object in inventory
 *   256   Object in room
 *   512   Object in world
 *  1024   Object in equipment
 *  2048   No target is OK
 *  4096   Direction is the target (?)
 *  8192   Also a direction, apparently (hence the question mark above)
 * 16384   A single word of text is the target
 * 32768   The full command line is the target
 */
struct skill_data skills[MAX_SKILLS] = {
    /*   0  Warrior skills */
    { "barehanded", PROF_WARRIOR, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 25, 1, 0, PLRSPEC_NONE },
    { "slashing", PROF_WARRIOR, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "concussion", PROF_WARRIOR, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 15, 1, 0, PLRSPEC_NONE },
    { "whips/flails", PROF_WARRIOR, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "piercing", PROF_WARRIOR, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "spears", PROF_WARRIOR, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "axes", PROF_WARRIOR, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "natural attacks", PROF_WARRIOR, 1, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "swimming", PROF_RANGER, 2, NULL, POSITION_FIGHTING, 0, 0, 16, 25, 1, 0, PLRSPEC_NONE },
    { "two-handed", PROF_WARRIOR, 1, NULL, POSITION_FIGHTING, 0, 0, 16, 40, 1, 0, PLRSPEC_NONE },
    { "weapon mastery", PROF_WARRIOR, 1, NULL, POSITION_FIGHTING, 0, 0, 16, 80, 1, 0, PLRSPEC_NONE },

    /* 11 */
    { "parry", PROF_WARRIOR, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "kick", PROF_WARRIOR, 2, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "bash", PROF_WARRIOR, 5, NULL, POSITION_FIGHTING, 0, 12, 16, 30, 1, 0, PLRSPEC_NONE },
    { "rescue", PROF_WARRIOR, 3, NULL, POSITION_FIGHTING, 0, 0, 16, 10, 1, 0, PLRSPEC_NONE },
    { "berserk", PROF_WARRIOR, 7, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "find weakness", PROF_WARRIOR, 20, NULL, POSITION_FIGHTING, 0, 0, 16, 40, 1, 0, PLRSPEC_NONE },
    { "block exit", PROF_WARRIOR, 10, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "wild swing", PROF_WARRIOR, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 65, 0, PLRSPEC_WILD },
    { "leadership", PROF_WARRIOR, 0, NULL, POSITION_STANDING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "riposte", PROF_RANGER, 20, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },

    /*  21 Ranger skills */
    { "dodge", PROF_RANGER, 1, NULL, POSITION_FIGHTING, 0, 0, 16, 24, 1, 0, PLRSPEC_NONE },
    { "fast attack", PROF_RANGER, 3, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "sneak", PROF_RANGER, 5, NULL, POSITION_FIGHTING, 0, 0, 16, 24, 1, 0, PLRSPEC_NONE },
    { "hide", PROF_RANGER, 2, NULL, POSITION_FIGHTING, 0, 4, 16, 20, 1, 0, PLRSPEC_NONE },
    { "ambush", PROF_RANGER, 6, NULL, POSITION_FIGHTING, 0, 20, 16, 24, 1, 0, PLRSPEC_NONE },
    { "track", PROF_RANGER, 0, NULL, POSITION_FIGHTING, 0, 8, 10, 24, 1, 0, PLRSPEC_NONE },
    { "pick lock", PROF_RANGER, 1, NULL, POSITION_FIGHTING, 0, 0, 16, 24, 1, 0, PLRSPEC_NONE },
    { "search", PROF_RANGER, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 20, 1, 0, PLRSPEC_NONE },
    { "animals", PROF_RANGER, 10, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_PETS },
    { "gather herbs", PROF_RANGER, 5, NULL, POSITION_FIGHTING, 0, 10, 16, 24, 1, 0, PLRSPEC_NONE },

    /* 31 */
    { "stealth", PROF_RANGER, 2, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_STLH },
    { "awareness", PROF_RANGER, 1, NULL, POSITION_FIGHTING, 0, 0, 16, 20, 1, 0, PLRSPEC_NONE },
    { "ride", PROF_RANGER, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 20, 1, 0, PLRSPEC_NONE },
    { "accuracy", PROF_RANGER, 11, NULL, POSITION_FIGHTING, 0, 0, 16, 20, 1, 0, PLRSPEC_NONE },
    { "tame", PROF_RANGER, 2, NULL, POSITION_STANDING, 0, 60, 2, 30, 1, 0, PLRSPEC_PETS },
    { "calm", PROF_RANGER, 1, NULL, POSITION_FIGHTING, 0, 6, 16, 16, 1, 0, PLRSPEC_PETS },
    { "whistle", PROF_RANGER, 1, NULL, POSITION_RESTING, 0, 6, 1, 30, 65, 0, PLRSPEC_PETS },
    { "stalking", PROF_RANGER, 1, NULL, POSITION_FIGHTING, 0, 6, 1, 30, 65, 0, PLRSPEC_STLH },
    { "travelling", PROF_RANGER, 6, NULL, POSITION_FIGHTING, 0, 0, 1, 30, 10, 0, PLRSPEC_NONE },
    { "recruit", PROF_GENERAL, 1, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },

    /* 41 Cleric skills */
    { "detect hidden", PROF_CLERIC, 1, spell_detect_hidden, POSITION_STANDING, 0, 10, 40, 10, 1, 0,
        PLRSPEC_NONE },
    { "evasion", PROF_CLERIC, 2, spell_evasion, POSITION_STANDING, 0, 10, 40, 10, 1, 0,
        PLRSPEC_PROT },
    { "poison", PROF_CLERIC, 14, spell_poison, POSITION_FIGHTING, 5, 19, 2066, 10, 1, 0,
        PLRSPEC_NONE },
    { "resist poison", PROF_CLERIC, 3, spell_resist_poison, POSITION_STANDING, 2, 25, 10, 10, 1, 0,
        PLRSPEC_NONE },
    { "curing saturation", PROF_CLERIC, 4, spell_curing, POSITION_STANDING, 1, 13, 10, 10, 1, 1,
        PLRSPEC_REGN },
    { "restlessness", PROF_CLERIC, 6, spell_restlessness, POSITION_STANDING, 0, 13, 10, 10, 1, 1,
        PLRSPEC_REGN },
    { "resist magic", PROF_CLERIC, 8, spell_resist_magic, POSITION_STANDING, 0, 10, 40, 10, 1, 0,
        PLRSPEC_NONE },
    { "slow digestion", PROF_CLERIC, 11, spell_slow_digestion, POSITION_STANDING, 1, 9, 10, 10, 1, 0,
        PLRSPEC_NONE },
    { "dispel regeneration", PROF_CLERIC, 13, spell_dispel_regeneration, POSITION_STANDING, 3, 23, 2,
        10, 1, 0, PLRSPEC_NONE },
    { "insight", PROF_CLERIC, 6, spell_insight, POSITION_FIGHTING, 2, 12, 26, 10, 1, 0,
        PLRSPEC_NONE },

    /* 51 */
    { "pragmatism", PROF_CLERIC, 6, spell_pragmatism, POSITION_FIGHTING, 5, 12, 26, 10, 1, 0,
        PLRSPEC_NONE },
    { "haze", PROF_CLERIC, 5, spell_haze, POSITION_FIGHTING, 1, 16, 2066, 10, 1, 0, PLRSPEC_ILLU },
    { "fear", PROF_CLERIC, 12, spell_fear, POSITION_FIGHTING, 5, 12, 18, 10, 1, 1, PLRSPEC_ILLU },
    { "divination", PROF_CLERIC, 13, spell_divination, POSITION_STANDING, 2, 24, 1, 10, 1, 0,
        PLRSPEC_NONE },
    { "rend", PROF_WARRIOR, 21, NULL, /* 55 */
        POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "sanctuary", PROF_CLERIC, 16, spell_sanctuary, POSITION_STANDING, 2, 36, 10, 10, 1, 0,
        PLRSPEC_NONE },
    { "vitality", PROF_CLERIC, 11, spell_vitality, POSITION_STANDING, 5, 12, 10, 10, 1, 1,
        PLRSPEC_REGN },
    { "terror", PROF_CLERIC, 18, spell_terror, POSITION_FIGHTING, 5, 15, 1, 10, 1, 1, PLRSPEC_ILLU },
    { "refresh all", PROF_CLERIC, 19, NULL, POSITION_STANDING, 10, 24, 10, 10, 1, 0, PLRSPEC_REGN },
    { "enchant weapon", PROF_CLERIC, 20, spell_enchant_weapon, /* 60 */
        POSITION_STANDING, 50, 72, 1152, 10, 1, 0, PLRSPEC_NONE },

    /* 61 */
    { "archery", PROF_RANGER, 1, NULL, POSITION_FIGHTING, 0, 0, 16, 24, 1, 0, PLRSPEC_NONE },
    { "summon", PROF_MAGE, 17, spell_summon, POSITION_STANDING, 50, 36, 6, 10, 1, 0, PLRSPEC_NONE },
    { "hallucinate", PROF_CLERIC, 3, spell_hallucinate, POSITION_STANDING, 2, 10, 18, 10, 1, 0,
        PLRSPEC_NONE },
    { "regeneration", PROF_CLERIC, 15, spell_regeneration, POSITION_STANDING, 5, 15, 10, 10, 1, 1,
        PLRSPEC_REGN },
    { "guardian", PROF_CLERIC, 10, spell_guardian, /* 65 */
        POSITION_STANDING, 30, 55, 32768, 10, 65, 0, PLRSPEC_GRDN },
    { "infravision", PROF_CLERIC, 17, spell_infravision, POSITION_STANDING, 2, 15, 40, 10, 1, 0,
        PLRSPEC_NONE },
    { "curse", PROF_CLERIC, 1, spell_curse, POSITION_FIGHTING, 1, 12, 18, 10, 1, 0, PLRSPEC_NONE },
    { "revive", PROF_CLERIC, 1, spell_revive, POSITION_FIGHTING, 2, 12, 18, 10, 1, 0, PLRSPEC_NONE },
    { "detect magic", PROF_CLERIC, 1, spell_detect_magic, POSITION_STANDING, 0, 10, 40, 10, 1, 0,
        PLRSPEC_NONE },
    { "shift", PROF_CLERIC, 30, spell_shift, POSITION_STANDING, 55, 36, 10, 10, 1, 0, PLRSPEC_NONE },
    /* if min_mana is changed on shift, then change amount in spell_pa */

    /*  71 Magic spells */
    { "magic missile", PROF_MAGE, 0, spell_magic_missile, POSITION_FIGHTING, 6, 11, 18, 10, 1, 0,
        PLRSPEC_NONE },
    { "reveal life", PROF_MAGE, 5, spell_reveal_life, POSITION_STANDING, 3, 3, 1, 10, 1, 0,
        PLRSPEC_NONE },
    { "locate living", PROF_MAGE, 2, spell_locate_living, POSITION_STANDING, 5, 19, 1, 10, 1, 0,
        PLRSPEC_NONE },
    { "cure self", PROF_MAGE, 3, spell_cure_self, POSITION_STANDING, 12, 36, 32, 10, 1, 0,
        PLRSPEC_REGN },
    { "chill ray", PROF_MAGE, 4, spell_chill_ray, POSITION_FIGHTING, 8, 15, 18, 10, 1, 0,
        PLRSPEC_COLD },
    { "blink", PROF_MAGE, 5, spell_blink, POSITION_FIGHTING, 5, 13, 32, 10, 1, 0, PLRSPEC_TELE },
    { "freeze", PROF_MAGE, 18, spell_freeze, POSITION_STANDING, 5, 37, TAR_DIRECTION, 10, 1, 0,
        PLRSPEC_COLD },
    { "lightning bolt", PROF_MAGE, 7, spell_lightning_bolt, POSITION_FIGHTING, 10, 17, 18, 10, 1, 0,
        PLRSPEC_LGHT },
    { "vitalize self", PROF_MAGE, 9, spell_vitalize_self, POSITION_STANDING, 12, 36, 32, 10, 1, 0,
        PLRSPEC_REGN },
    { "flash", PROF_MAGE, 6, spell_flash, POSITION_FIGHTING, 5, 7, 2050, 10, 33, 0, PLRSPEC_LGHT },

    /* 81 */
    { "earthquake", PROF_MAGE, 20, spell_earthquake, POSITION_FIGHTING, 15, 21, 1, 10, 1, 0,
        PLRSPEC_NONE },
    { "create light", PROF_MAGE, 1, spell_create_light, POSITION_STANDING, 5, 10, 1, 10, 1, 0,
        PLRSPEC_NONE },
    { "death ward", PROF_CLERIC, 20, spell_death_ward, POSITION_STANDING, 0, 72, 40, 1, 1, 0,
        PLRSPEC_PROT },
    { "dark bolt", PROF_MAGE, 9, spell_dark_bolt, POSITION_FIGHTING, 10, 17, 18, 10, 1, 0,
        PLRSPEC_DARK },
    { "mist of baazunga", PROF_MAGE, 27, spell_mist_of_baazunga, POSITION_STANDING, 50, 72, 36, 1, 1,
        0, PLRSPEC_DARK },
    { "mind block", PROF_CLERIC, 3, spell_mind_block, POSITION_STANDING, 5, 15, 40, 10, 1, 0,
        PLRSPEC_NONE },
    { "remove poison", PROF_CLERIC, 8, spell_remove_poison, POSITION_STANDING, 2, 25, 10, 10, 1, 0,
        PLRSPEC_NONE },
    { "beacon", PROF_MAGE, 1, spell_beacon, POSITION_STANDING, 15, 48, 16384 | 2048, 1, 65, 0,
        PLRSPEC_TELE },
    { "protection", PROF_CLERIC, 0, spell_protection, POSITION_STANDING, 5, 21, 32768, 1, 65, 0,
        PLRSPEC_PROT },
    { "blaze", PROF_MAGE, 18, spell_blaze, POSITION_FIGHTING, 15, 21, 1, 1, 65, 1, PLRSPEC_FIRE },

    /* 91 */
    { "firebolt", PROF_MAGE, 11, spell_firebolt, POSITION_FIGHTING, 13, 23, 18, 10, 1, 0,
        PLRSPEC_FIRE },
    { "relocate", PROF_MAGE, 24, spell_relocate, POSITION_STANDING, 30, 72, 8192, 10, 1, 0,
        PLRSPEC_TELE },
    { "cone of cold", PROF_MAGE, 14, spell_cone_of_cold, POSITION_FIGHTING, 15, 21, 18, 10, 1, 0,
        PLRSPEC_COLD },
    { "identify", PROF_MAGE, 18, spell_identify, POSITION_STANDING, 70, 50, 128, 10, 1, 0,
        PLRSPEC_NONE },
    { "bend time", PROF_RANGER, 30, NULL, POSITION_FIGHTING, 0, 6, 65, 10, 1, 1, PLRSPEC_NONE },
    { "fireball", PROF_MAGE, 21, spell_fireball, POSITION_FIGHTING, 18, 31, 18, 10, 1, 0,
        PLRSPEC_FIRE },
    { "locate life", PROF_MAGE, 20, NULL, POSITION_STANDING, 20, 16, 1, 10, 1, 0, PLRSPEC_NONE },
    { "searing darkness", PROF_MAGE, 21, spell_searing_darkness, POSITION_FIGHTING, 18, 31, 18, 10,
        1, 0, PLRSPEC_DARK },
    { "lightning strike", PROF_MAGE, 19, spell_lightning_strike, POSITION_FIGHTING, 10, 27, 18, 10,
        1, 0, PLRSPEC_LGHT },
    { "word of pain", PROF_MAGE, 0, spell_word_of_pain, POSITION_FIGHTING, 6, 11, 18, 10, 1, 0,
        PLRSPEC_NONE },

    /* 101 */
    { "word of sight", PROF_MAGE, 5, spell_word_of_sight, POSITION_STANDING, 3, 3, 1, 10, 1, 0,
        PLRSPEC_NONE },
    { "word of agony", PROF_MAGE, 14, spell_word_of_agony, POSITION_FIGHTING, 15, 21, 18, 10, 1, 0,
        PLRSPEC_NONE },
    { "shout of pain", PROF_MAGE, 20, spell_shout_of_pain, POSITION_FIGHTING, 15, 21, 1, 10, 1, 0,
        PLRSPEC_NONE },
    { "word of shock", PROF_MAGE, 6, spell_word_of_shock, POSITION_FIGHTING, 5, 7, 2050, 10, 33, 0,
        PLRSPEC_NONE },
    { "spear of darkness", PROF_MAGE, 21, spell_spear_of_darkness, POSITION_FIGHTING, 16, 29, 18, 10,
        1, 0, PLRSPEC_DARK },
    { "leach", PROF_MAGE, 4, spell_leach, POSITION_FIGHTING, 8, 15, 18, 10, 1, 0, PLRSPEC_NONE },
    { "black arrow", PROF_MAGE, 11, spell_black_arrow, POSITION_FIGHTING, 13, 23, 18, 10, 1, 0,
        PLRSPEC_DARK },
    { "shield", PROF_MAGE, 2, spell_shield, POSITION_STANDING, 5, 9, 32, 10, 1, 0, PLRSPEC_NONE },
    { "detect evil", PROF_MAGE, 3, spell_detect_evil, POSITION_STANDING, 2, 12, 1, 5, 1, 0,
        PLRSPEC_NONE },
    { "blind", PROF_RANGER, 27, NULL, POSITION_FIGHTING, 20, 10, 16, 24, 1, 1, PLRSPEC_NONE },

    /* 111 */
    { "confuse", PROF_CLERIC, 1, spell_confuse, POSITION_FIGHTING, 10, 28, 16, 10, 65, 1,
        PLRSPEC_ILLU },
    { "expose elements", PROF_MAGE, 1, spell_expose_elements, POSITION_FIGHTING, 70, 5, 16, 1, 1, 0,
        PLRSPEC_NONE },
    { "bite", PROF_WARRIOR, 9, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "swipe", PROF_WARRIOR, 1, NULL, POSITION_STANDING, 0, 0, 16, 1, 1, 0, PLRSPEC_NONE },
    { "maul", PROF_WARRIOR, 12, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 1, PLRSPEC_NONE },
    { "asphyxiation", PROF_GENERAL, 1, NULL, POSITION_STANDING, 5, 2, 6, 1, 1, 1, PLRSPEC_NONE },
    { "Power of Arda", PROF_GENERAL, 1, NULL, POSITION_STANDING, 5, 2, 6, 1, 1, 1, PLRSPEC_NONE },
    { "activity", PROF_GENERAL, 1, NULL, POSITION_STANDING, 5, 2, 6, 1, 1, 1, PLRSPEC_NONE },
    { "rage", PROF_GENERAL, 1, NULL, POSITION_STANDING, 5, 2, 6, 1, 1, 0, PLRSPEC_NONE },
    { "anger", PROF_GENERAL, 1, NULL, POSITION_STANDING, 5, 2, 6, 1, 1, 0, PLRSPEC_NONE },

    /* 121 */
    { "animal language", PROF_GENERAL, 1, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "human language", PROF_GENERAL, 1, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "orcish language", PROF_GENERAL, 1, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "mark", PROF_RANGER, 15, NULL, POSITION_FIGHTING, 20, 0, 16, 24, 1, 1, PLRSPEC_NONE },
    { "trash", PROF_MAGE, 1, NULL, POSITION_STANDING, 5, 2, 6, 1, 1, 0, PLRSPEC_NONE },
    { "trash", PROF_MAGE, 1, NULL, POSITION_STANDING, 5, 2, 6, 1, 1, 0, PLRSPEC_NONE },
    { "nothing", PROF_GENERAL, 0, NULL, POSITION_DEAD, 5, 2, 6, 1, 1, 0, PLRSPEC_NONE },
    { "wind blast", PROF_RANGER, 24, NULL, POSITION_FIGHTING, 20, 12, TAR_IGNORE, 24, 1, 0,
        PLRSPEC_NONE },
    { "Fame War", PROF_GENERAL, 1, NULL, POSITION_STANDING, 5, 2, 6, 1, 1, 1, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    /* 131 */
    { "defend", PROF_WARRIOR, 0, NULL, POSITION_FIGHTING, 0, 0, 0, 10, 65, 1, PLRSPEC_DFND },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    /* 141 */
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    /* 151 */
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "smash", PROF_WARRIOR, 20, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "frenzy", PROF_WARRIOR, 30, NULL, POSITION_FIGHTING, 0, 0, 65, 10, 1, 1, PLRSPEC_NONE },
    { "stomp", PROF_GENERAL, 27, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
    { "cleave", PROF_WARRIOR, 25, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0, PLRSPEC_NONE },
    { "overrun", PROF_WARRIOR, 15, NULL, POSITION_STANDING, 0, 0, 65, 10, 1, 1, PLRSPEC_NONE },
    { "mass regeneration", PROF_CLERIC, 15, spell_mass_regeneration, POSITION_STANDING, 50, 15, 1,
        10, 1, 1, PLRSPEC_NONE },
    { "mass vitality", PROF_CLERIC, 11, spell_mass_vitality, POSITION_STANDING, 50, 12, 1, 10, 1, 1,
        PLRSPEC_NONE },
    { "mass insight", PROF_CLERIC, 6, spell_mass_insight, POSITION_FIGHTING, 25, 12, 1, 10, 1, 0,
        PLRSPEC_NONE },
    /* 161 */
    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 5, 1, 0, PLRSPEC_NONE },
};

byte language_number = 3;
// These language skill values are defined in structs.h
byte language_skills[] = { LANG_ANIMAL, LANG_HUMAN, LANG_ORC };

char guildmaster_number = 61;

struct skill_teach_data guildmasters[] = {
    { // ALL SKILLS (1)
        "Hello, $N! I can teach you these skills:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            /*51*/ 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            100, 100, 100, 100, 100, 100, 100, 100 } },
    { // VINYANOST WARRIOR (2)
        "Hello, $N! I can teach you these skills:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 100, 100, 100, 100, 100, 100, 0, 0, 100, 100, 100, 100, 100, 100, 100, 100,
            0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0,
            0, 0, 0, 0, 0, 0 } },
    { // VINYANOST RANGER (3)
        "Hello, $N! I can teach you these skills:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 100, 60, 0, 100, 60, 60, 0, 40, 0, 100, 0, 70, 75, 100, 0, 50, 0, 0, 40,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // VINYANOST MYSTIC (4)
        "Hello, $N! I can teach you to use these powers:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 80, 40, 0, 45, 35, 0, 0, 100, 0, 100,
            /*51*/ 100, 0, 0, 100, 0, 0, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 80, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 50, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // VINYANOST MAGE (5)
        "Hello, $N! I can only teach you these spells:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            100, 0, 20, 100, 75, 100, 0, 100, 100, 0, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 65, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // WEST HILL MAGE (6)
        "Hello, $N! I can teach you these spells:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            100, 0, 20, 100, 75, 100, 0, 100, 100, 0, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 65, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // TRAVELLER (MAGE) (7)
        "Hello, $N! I can teach you these spells:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 100, 80, 100, 100, 0, 100, 100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            100, 100, 100, 0, 0, 100, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 100, 100, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // OUTPOST RANGER (8)
        "Hello, $N! I can teach you these skills:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 100, 80, 100, 100, 100, 100, 10, 100, 0, 100, 100, 100, 75, 100, 70, 0, 0, 100, 70,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // URUK WARRIOR (9)
        "You can train in these skills:",
        "$n snarls 'Get out!'",
        "$n trains $N.",
        "You practice it for a while.",
        "The guildmaster tells you 'I won't teach you any better.'",
        "You're already perfect in this.",
        { 0, 100, 100, 100, 100, 100, 100, 0, 0, 0, 100, 100, 0, 0, 0, 100, 100, 0, 0, 0,
            100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // URUK RANGER (10)
        "You can train in these skills:",
        "$n snarls 'Get out!'",
        "$n trains $N.",
        "You practice it for a while.",
        "The guildmaster tells you 'I won't teach you any better.'",
        "You're already perfect in this.",
        { 0,

            0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100,
            0, 0, 100, 100, 100, 100, 100, 100, 0, 0, 100, 100, 100, 100, 0, 0, 0, 100, 30, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // INTERLOPER (11)
        "You can try to learn these powers:",
        "$n snarls 'Leave you worthless bastard!'",
        "$n explains something to $N",
        "You practice it for a while.",
        "The guildmaster smirks and tells you 'You shouldn't know this any better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 30, 60, 30, 0, 0, 100, 0, 60, 60,
            /*51*/ 60, 60, 100, 30, 0, 0, 0, 100, 0, 0, 0, 0, 30, 60, 100, 0, 30, 0, 100, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 60, 60, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // URUK MAGE (12)
        "You can try to learn these spells:",
        "$n snarls 'Leave you worthless bastard!'",
        "$n explains something to $N",
        "You practice it for a while.",
        "The mage smirks and tells you 'You shouldn't know this any better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0,
            100, 0, 60, 100, 0, 60, 0, 0, 60, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0,
            0, 30, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 60, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // LAKETOWN WARRIOR (13)
        "Hello, $N! I can teach you these skills:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 100, 100, 100, 100, 100, 100, 0, 0, 100, 100, 100, 100, 100, 0, 100, 100,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0,
            0, 0, 0, 0, 0, 0 } },
    { // LAKETOWN LIBRARIAN (languages) (14)
        "Hello, $N! I can teach you these languages:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 100, 100, 0, 0, 0, 0 } },
    { // LAKETOWN MYSTIC (15)
        "Hello, $N! I can teach you to use these powers:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 100, 100, 100, 80, 100, 100, 100, 0, 100, 100,
            /*51*/ 40, 100, 0, 30, 0, 20, 100, 0, 0, 0, 0, 0, 70, 80, 0, 100, 100, 0, 100, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 100, 0, 100, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // LAKETOWN MAGE (16)
        "Hello, $N! I can only teach you these spells:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0,
            100, 100, 0, 60, 100, 60, 0, 85, 60, 80, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0,
            100, 25, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 60, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // MAETHELBURG WARRIOR (17)
        "Hello, $N! I can teach you these skills:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 100, 100, 100, 100, 100, 100, 0, 0, 100, 100, 100, 0, 100, 100, 100, 30,
            0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0,
            0, 0, 0, 0, 0, 0 } },
    { // MAETHELBURG RANGER (18)
        "Hello, $N! I can teach you these skills:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            60, 100, 0, 60, 100, 80, 80, 0, 100, 100, 100, 0, 100, 100, 100, 100, 100, 100, 0, 80,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // MAETHELBURG MYSTIC (19)
        "Hello, $N! I can teach you to use these powers:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 50, 80, 40, 100, 100, 30, 80, 100, 0, 100,
            /*51*/ 0, 70, 100, 100, 0, 100, 70, 100, 0, 0, 0, 0, 100, 100, 0, 0, 100, 100, 50, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // MAETHELBURG MAGE (20)
        "Hello, $N! I can only teach you these spells:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // ENCHANT (21)
        "Hello, $N! I can only teach you these powers:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // GUARDIAN (22)
        "Hello, $N! I can only teach you these powers:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // PICK LOCK (23)
        "Hello, $N! I can only teach you these skills:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // ORC WARRIOR (24)
        "You can train in these skills:",
        "$n snarls 'Get out!'",
        "$n trains $N.",
        "You practice it for a while.",
        "The guildmaster tells you 'I won't teach you any better.'",
        "You're already perfect in this.",
        { 0, 100, 100, 100, 60, 100, 30, 0, 0, 60, 100, 100, 100, 60, 0, 30, 30, 0, 30, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // ORC RANGER (25)
        "You can train in these skills:",
        "$n snarls 'Get out!'",
        "$n trains $N.",
        "You practice it for a while.",
        "The guildmaster tells you 'I won't teach you any better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            100, 0, 0, 100, 100, 100, 100, 100, 100, 0, 0, 100, 100, 100, 100, 0, 0, 0, 100, 30,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // ORC PRIEST (26)
        "You can try to learn these powers:",
        "$n snarls 'Leave you worthless bastard!'",
        "$n explains something to $N",
        "You practice it for a while.",
        "The guildmaster smirks and tells you 'You shouldn't know this any better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 30, 60, 30, 0, 0, 60, 0, 60, 60,
            /*51*/ 60, 60, 100, 30, 0, 0, 0, 100, 0, 0, 0, 0, 30, 60, 0, 0, 30, 0, 100, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 60, 60, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // ORC SHAMAN (27)
        "You can try to learn these spells:",
        "$n snarls 'Leave you worthless bastard!'",
        "$n explains something to $N",
        "You practice it for a while.",
        "The guildmaster smirks and tells you 'You shouldn't know this any better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0,

            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 60, 100, 0,
            60, 0, 0, 60, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 100, 30, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0 } },
    { // LAKETOWN RANGER (wanderer) (28) (Aroden)
        "Hello, $N! I can only teach you these skills:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 100, 100, 100, 100, 100,
            100, 100, 100, 100, 100, // 100,100, 40,  0, 60,100,  0,
                                     // 80,  0, 60,
            100, 100, 100, 100, 100, 100, 100, 100, 100, 0, // 100,100,  5,100,  0,  0, 20,  0,100,
                                                            // 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // URUK ENCHANT/GUARDIAN (29)
        "Hello, $N! I can only teach you these powers:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // MAGUS (30)
        "You can try to learn these spells:",
        "$n snarls 'Leave you worthless bastard!'",
        "$n explains something to $N",
        "You practice it for a while.",
        "The magus smirks and tells you 'You shouldn't know this any better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 33, 1, 1, 0, 0, 0, 30, 66,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 66, 0, 0, 0, 100, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0,
            0, 100, 0, 0, 0, 100, 100, 0, 100, 0, 0, 100, 0, 0, 0, 0, 100,
            0, 100, 0, 0, 0, 0, 0, 100, 0, 100, 0, 0, 0, 0, 0, 100, 100,
            100, 100, 100, 100, 100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 100, 0, 0, 0, 0, 0 } },
    { // Elven Halls RANGER (31)
        "Hello, $N! I am able to teach you these skills:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'I cannot teach this better.'",
        "You're already a master in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 100, 60, 0, 100, 60, 60, 0, 40, 0, 100, 0, 70, 75, 100, 0, 50, 0, 0, 40,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { // Elven Halls WARRIOR (32)
        "Hello, $N! I can teach you these skills:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 100, 100, 100, 100, 100, 100, 0, 0, 100, 100, 100, 100, 100, 100, 100, 100,
            0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0,
            0, 0, 0, 0, 0, 0 } },
    { // Elven Halls MAGE (33)
        "Hello, $N! I can only teach you these spells:",
        "Sorry, I may not teach you.",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "The guildmaster bows and tells you 'Sorry, I can not teach you better.'",
        "You're already perfect in this.",
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            100, 0, 20, 100, 75, 100, 0, 100, 100, 0, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 65, 40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* DG uruk ranger (27532) (34) */
    { "Be quick, $N! I don't want to waste too much time on you!",
        "",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "$n tells you 'You've wasted enough of my time already!'",
        "You are already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 30, 100, 30,
            100, 0, 100, 0, 0, 30, 0, 100, 100, 0, 0, 0, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0 } },

    /* DG slithe ranger (27702) (35) */
    { "I can teach you much about the ways of the forest, but be quick!",
        "",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "$n whispers 'It would be more than my life's worth to teach you better.'",
        "You're already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 60, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 60, 100, 100, 0, 100, 100, 100, 30, 100, 0, 0, 60, 60, 100,
            100, 0, 0, 0, 60, 30, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* surka ranger (28418) (36) */
    { "What, snaga?  You want to know about woodcraft?",
        "",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "$n lisps 'You should go find an elf, they teach better.'",
        "You're already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0,
            0, 0, 0, 100, 75, 0, 0, 0, 100, 100, 100, 100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0 } },

    /* DG glhrush (28416) (37) */
    { "Assassination is a fine art, what does a snaga like you need to know?",
        "",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "$n whispers 'You don't need to know any more about that.'",
        "You are already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0,
            0, 0, 0, 100, 0, 0, 0, 0, 100, 100, 100, 100, 0, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0 } },

    /* DG troll warrior (27505) (38) */
    { "What want to know?",
        "",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "$n says 'You know lots, I not know more.'",
        "You're already perfect in this.",

        { 0, 60, 100, 100, 0, 60, 60, 0, 0, 100, 0, 100, 100, 100, 0, 100, 0, 0, 100, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* DG uruk warrior (27533) (39) */
    { "What do you want to know about the ways of the fighting Uruk-hai?",
        "",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "$n tells you 'This is as good as you can be.'",
        "You're already perfect in this.",

        { 0, 100, 100, 100, 60, 100, 100, 0, 0, 100, 100, 100, 100, 100, 0, 30, 30, 0, 30, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* DG human warrior, Logarn (27701) (40) */
    { "Hello, $N.  I can teach you these arts of armed combat:",
        "",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "$n tells you 'You've reached your limit.'",
        "You're already perfect in this.",

        { 0, 100, 30, 30, 100, 100, 30, 0, 0, 60, 30, 100, 0, 60, 0, 60, 60, 0, 100, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* DG sorceror (27705) (41) */
    { "Greetings, $N.  I know much about the arts of offensive magic:",
        "",
        "$n explains something to $N.",
        "You practice it for a while.",
        "$n scowls and says, 'I can't teach you any better!'",
        "You're already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 100, 0, 0,
            0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* DG sage (27703) (42) */
    { "Hello, $N.  I can teach you about magical legend, lore, and divination:",
        "",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "$n tells you 'I've yet to learn better myself.'",
        "You're already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 100, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 100, 100, 0, 0,
            0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0 } },

    /* DG magician (27704) (43) */
    { "I practice transmutation and conjuring, what do you want to know?",
        "",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "$n says 'This is known no better to mortal minds.'",
        "You're already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 100, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* DG spirit (27706) (44) */
    { "What do you wish to know about the ways of the spirit world?",
        "",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "$n says 'There is nothing more to know.'",
        "You're already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100,

            100, 0, 60, 0, 0, 0, 0, 60, 0, 0, 0, 0, 0, 0, 0, 0, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* DG witch doctor (27707) (45) */
    { "What can I teach you about healing and poisons?",
        "",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "$n scowls at you and mutters 'I won't teach you all of my tricks, idiot.'",
        "You're already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 100, 100, 100, 0, 0, 100, 0,

            0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0 } },

    /* DG acolyte (27708) (46) */
    { "What do you want to know about the shadow world?",
        "",
        "$n teaches $N for a while.",
        "You practice it for a while.",
        "$n tells you 'I don't know any more about this.'",
        "You're already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 100, 0, 60, 30, 30, 0, 100, 0, 30, 30, 0, 30, 0, 0, 0, 60, 30, 0, 0, 0, 0, 0, 30, 0,
            0, 60, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30, 0, 100, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 30, 0, 0, 0, 0,
            0, 0 } },

    /* GG glass-eyed shaman (32200) (47)*/
    { "As the shaman turns his head, his glass eye wobbles furiously, 'What do you want?'",
        "",
        "$n speaks to $N for a while.",
        "$n explains it to you.",
        "$n tells you 'There isn't more for you to know.'",
        "You're already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 100, 100, 100, 60, 20, 100, 0, 0, 100, 0, 0, 0, 0, 100,
            100, 0, 0, 0, 0, 0, 95, 60, 0, 100, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 80, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* DG gorbush the scarred (27724) (48) */
    { "$n tells you 'You'd better learn to fight, snaga!'",
        "",
        "$n trains with $N vigorously.",
        "$n trains you intensely.",
        "$n tells you 'Your body won't let you train any further, weakling!'",
        "You're already perfect in this.",

        { 0, 100, 60, 60, 30, 100, 0, 0, 0, 100, 0, 100, 30, 60, 0, 100, 0, 0, 30, 60,
            0, 100, 0, 0, 0, 0, 60, 0, 0, 0, 0, 0, 0, 60, 0, 0, 0, 100, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* GG gurokh, orcish captain (32527) (49) */
    { "$n tells you 'Are you prepared to train, snaga?'",
        "",
        "$n shows $N how to handle a weapon.",
        "$n shows you how to grip a weapon.",
        "$n tells you 'You can't handle any more.'",
        "You're already perfect in this.",

        { 0, 100, 100, 100, 100, 100, 100, 0, 0, 80, 0, 90, 100, 100, 0, 30, 100, 0, 100, 100,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* GG dhamgul, orcish ranger (32530) (50) */
    { "$n tells you 'You'd better not waste too much of my time.'",
        "",
        "$n trains $N.",
        "$n trains you.",
        "$n tells you 'You've wasted enough of my time!'",
        "You're already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 40, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 80, 60, 90, 100, 60, 100, 5, 50, 0, 0, 0, 20, 100,
            0, 100, 100, 100, 100, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* DG goblin shaman (27730) (51) */
    { "The shaman breaks his gaze at the ceiling and says 'Do you think you can learn my craft, "
      "idiot?'",
        "",
        "$n recites something with $N multiple times.",
        "$n recites the spell with you.",
        "$n ignores your request.",
        "You're already perfect in this",

        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 100, 0, 0, 0, 0, 0, 0,
            100,

            0, 0, 100, 100, 0, 0, 0, 100, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 100, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0 } },

    /* maelor, orcish illusionist (32529) (52) */
    { "$n tells you 'Make it quick.'",
        "",
        "$n teaches $N.",
        "$n teaches you about it.",
        "Your request falls on deaf ears.",
        "You're already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 60, 100, 100, 75, 80, 0, 0, 100, 80, 100,

            100, 100, 100, 0, 0, 0, 50, 100, 0, 0, 0, 0, 100, 50, 60, 0, 80,
            80, 0, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 50, 60, 60, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    /* DG: unnamed orcish warrior (27601) (53) */
    { "$n tells you, 'You'd better be willing, snaga.'",
        "",
        "$n trains $N rigorously.",
        "$n trains you mercilessly.",
        "$n grimaces and says, 'You couldn't handle anymore, weakling.'",
        "You are already perfect in this.",

        { 0, 0, 100, 30, 0, 0, 100, 0, 0, 100, 30, 100, 0, 100, 0, 100, 30, 0, 100, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { /*Uruk lhuth in vinyanost tombs*/
        "$n tells you, 'Come learn my secrets.'",
        "",
        "$n trains $N rigorously.",
        "$n teachs you.",
        "$n shrugs, 'That is as good as i can teach.'",
        "You are already perfect in this.",

        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 47,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { /*Karvok Mob (55) */
        "You can train in these skills:",
        "$n snarls 'Get out!'",
        "$n trains $N.",
        "You practice it for a while.",
        "The guildmaster tells you, 'I won't teach you any better.'",
        "You're already perfect in this.",
        { 0, 25, 25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { /* Ungorod Uruk Swashbuckler 4647 (56)*/
        "You can train in these skills:",
        "$n snarls 'Get out!'",
        "$n trains $N.",
        "You practice it for a while.",
        "The guildmaster tells you 'I won't teach you any better.'",
        "You're already perfect in this.",
        { 0, 100, 100, 100, 100, 100, 100, 0, 25, 100, 100, 100, 0, 100, 0, 100, 100, 0, 0, 0,
            100, 100, 25, 0, 0, 0, 75, 25, 25, 0, 0, 0, 0, 75, 0, 0, 0, 0, 0, 25,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { /* ALT RotS Guildmaster Puke (57)*/
        "You can train in these skills:",
        "$n snarls 'Get out!'",
        "$n trains $N.",
        "You practice it for a while.",
        "The guildmaster tells you 'I won't teach you any better.'",
        "You're already perfect in this.",
        { 0, 100, 100, 100, 100, 100, 100, 0, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            0, 100, 0, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            100, 100, 100, 100, 100, 100, 0, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            /*51*/ 100, 100, 100, 100, 0, 100, 100, 100, 0, 100, 100, 100, 100, 100, 100, 100, 100,
            100, 100, 0, 100, 100, 100, 100, 100, 100, 0, 100, 100, 100, 100, 100, 100, 0,
            0, 0, 100, 100, 100, 100, 100, 100, 100, 100, 0, 100, 0, 0, 100, 0,
            /*101*/ 0, 0, 0, 0, 0, 0, 0, 100, 100, 0, 100, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 100, 0, 100, 0, 0, 0, 0, 0, 0, 0, 100, 0, 0, 0,
            0, 0, 0, 0, 0, 0 } },
    { /* ALT RotS Guildmasters (58)*/
        "You can train in these skills:",
        "$n snarls 'Get out!'",
        "$n trains $N.",
        "You practice it for a while.",
        "The guildmaster tells you 'I won't teach you any better.'",
        "You're already perfect in this.",
        { 0,
            /*01*/ 100, 100, 100, 100, 100, 100, 0, 100, 100, 100,
            /*11*/ 100, 100, 100, 0, 100, 100, 0, 100, 0, 100,
            /*21*/ 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            /*31*/ 100, 100, 100, 100, 100, 100, 100, 100, 100, 0,
            /*41*/ 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            /*51*/ 100, 100, 100, 100, 0, 0, 100, 100, 0, 100,
            /*61*/ 100, 100, 100, 100, 100, 0, 100, 100, 100, 0,
            /*71*/ 100, 100, 100, 100, 100, 100, 0, 0, 100, 0,
            /*81*/ 100, 0, 100, 100, 0, 100, 100, 100, 100, 100,
            /*91*/ 0, 100, 100, 100, 0, 0, 0, 100, 0, 0,
            /*101*/ 0, 0, 0, 0, 0, 0, 0, 100, 0, 0,
            /*111*/ 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*121*/ 0, 100, 0, 0, 0, 0, 0, 0, 0, 0,
            /*131*/ 100, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { /* Beorning Guildmaster VNUM:10310 (59)*/
        "You can train in these skills:",
        "$n snarls 'Get out!'",
        "$n trains $N.",
        "You practice it for a while.",
        "The guildmaster tells you 'I won't teach you any better.'",
        "You're already perfect in this.",
        { 0,
            /*01*/ 0, 0, 0, 0, 0, 0, 100, 10, 0, 0,
            /*11*/ 100, 0, 100, 100, 100, 100, 0, 0, 0, 0,
            /*21*/ 100, 0, 100, 100, 0, 100, 25, 100, 100, 100,
            /*31*/ 80, 100, 0, 0, 100, 100, 0, 100, 100, 0,
            /*41*/ 30, 0, 0, 25, 60, 40, 0, 30, 0, 0,
            /*51*/ 0, 0, 0, 0, 100, 0, 0, 0, 0, 0,
            /*61*/ 0, 0, 0, 0, 0, 0, 0, 20, 0, 0,
            /*71*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*81*/ 0, 0, 0, 0, 0, 0, 25, 0, 100, 0,
            /*91*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*111*/ 0, 0, 100, 100, 100, 0, 0, 0, 0, 0,
            /*121*/ 100, 100, 100, 0, 0, 0, 0, 0, 0, 0,
            /*131*/ 100, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { /* Olog-Hai Guildmaster VNUM: 32816 (60)*/
        "You can train in these skills:",
        "$n snarls 'Get out!'",
        "$n trains $N.",
        "You practice it for a while.",
        "The guildmaster tells you 'I won't teach you any better.'",
        "You're already perfect in this.",
        { 0,
            /*01*/ 100, 100, 100, 100, 100, 100, 100, 10, 100, 100,
            /*11*/ 100, 100, 100, 0, 100, 100, 0, 100, 0, 0,
            /*21*/ 100, 100, 40, 45, 0, 100, 100, 100, 70, 0,
            /*31*/ 0, 100, 100, 0, 100, 100, 100, 60, 100, 0,
            /*41*/ 0, 0, 0, 60, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*61*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*71*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*81*/ 0, 0, 0, 0, 0, 60, 0, 0, 100, 0,
            /*91*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*111*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*121*/ 0, 100, 0, 0, 0, 0, 0, 0, 0, 0,
            /*131*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*141*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*151*/ 0, 100, 100, 100, 0, 100, 100, 0, 0, 0,
            /*161*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*171*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*181*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { /* Haradrim Guildmaster VNUM:6605 (61) */
        "You can train in these skills:",
        "$n snarls 'Get out!'",
        "$n trains $N.",
        "You practice it for a while.",
        "The guildmaster tells you 'I won't teach you any better.'",
        "You're already perfect in this.",
        { 0,
            /*01*/ 100, 30, 60, 100, 100, 0, 0, 100, 100, 0,
            /*11*/ 100, 100, 0, 0, 90, 100, 0, 100, 0, 100,
            /*21*/ 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
            /*31*/ 100, 100, 100, 100, 100, 100, 100, 100, 100, 0,
            /*41*/ 0, 0, 0, 30, 0, 0, 0, 0, 0, 0,
            /*51*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*61*/ 100, 0, 0, 0, 100, 0, 0, 0, 60, 0,
            /*71*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*81*/ 0, 0, 0, 0, 0, 25, 0, 0, 100, 0,
            /*91*/ 0, 0, 0, 0, 100, 0, 0, 0, 0, 0,
            /*101*/ 0, 0, 0, 0, 0, 0, 0, 0, 0, 100,
            /*111*/ 100, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            /*121*/ 100, 100, 100, 100, 0, 0, 0, 100, 0, 0,
            /*131*/ 100, 0, 0, 0, 0, 0, 0, 0 } }
};

struct race_bodypart_data bodyparts[MAX_BODYTYPES] = {
    { { "", "", "", "", "", "", "", "", "", "", "" }, /* 0 */
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        0,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    { { "", "head", "body", "left leg", "right leg", "left foot", "right foot", "left arm",
          "right arm", "left hand", "right hand" }, /* 1 */
        { 0, 15, 35, 8, 8, 2, 2, 10, 10, 5, 5 },
        10,
        { 0, 6, 5, 7, 7, 8, 8, 10, 10, 9, 9 } },

    { { "", "head", "body", "left hindleg", "right hindleg", "left hindfoot", "right hindfoot",
          "left foreleg", "right foreleg", "left forefoot", "right forefoot" },
        { 0, 15, 37, 7, 7, 2, 2, 11, 11, 4, 4 },
        10,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    { { "", "head", "body", "leg", "leg", "leg", "leg", "leg", "leg", "leg", "leg" },
        { 0, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10 },
        10,
        { 0, 6, 0, 8, 8, 0, 0, 0, 0, 0, 0 } },

    { { "", "head", "body", "left leg", "right leg", "left wing", "right wing", "left claw",
          "right claw" },
        { 0, 20, 30, 8, 8, 15, 15, 2, 2 },
        8,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    { { "", "", "", "", "", "", "", "", "", "", "" }, /* 5 */
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        0,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    { { "", "", "", "", "", "", "", "", "", "", "" },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        0,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    { { "", "", "", "", "", "", "", "", "", "", "" },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        0,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    { { "", "", "", "", "", "", "", "", "", "", "" },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        0,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    { { "", "", "", "", "", "", "", "", "", "", "" },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        0,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    { { "", "", "", "", "", "", "", "", "", "", "" }, /* 10 */
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        0,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    { { "", "", "", "", "", "", "", "", "", "", "" },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        0,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    { { "", "", "", "", "", "", "", "", "", "", "" },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        0,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    { { "", "", "", "", "", "", "", "", "", "", "" },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        0,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },

    { { "", "", "", "", "", "", "", "", "", "", "" }, /* 14, can easily be added */
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        0,
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    { { "", "head", "body", "left hindleg", "right hindleg", "left hindfoot", "right hindfoot",
          "left foreleg", "right foreleg", "left claw", "right claw" }, /* 15 */
        { 0, 15, 35, 8, 8, 2, 2, 10, 10, 5, 5 },
        10,
        { 0, 6, 5, 7, 7, 8, 8, 10, 10, 9, 9 } },
};

int rev_dir[] = { 2, 3, 0, 1, 5, 4 };

int movement_loss[] = { /* movement cost with a good load BTW. min. is 3/4 .. 2->1 4->2.5 6->4 10 ->7times smaller */
    2, /* Inside     */
    3, /* City       */
    4, /* Field      */
    7, /* Forest     */
    10, /* Hills      */
    11, /* Mountains  */
    10, /* Swimming   */
    12, /* Unswimable */
    12, /* Underwater */
    3, /* Road       */
    12, /* Crack, from earthquake */
    9, /* dense forest*/
    12, /* swamp       */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

int num_of_object_materials = 14;
char* object_materials[] = {
    "Usual stuff", "cloth", "leather", "chain", "metal", "wood", "stone",
    "crystal", "gold", "silver", "mithril", "fur", "glass", "plant",
    "undefined", "undefined", "undefined", "undefined", "undefined", "undefined", "undefined"
};

// const
char* dirs[] = { "north", "east", "south", "west", "up", "down", "\n" };
char* refer_dirs[] = { "the north", "the east", "the south", "the west", "above", "below", "\n" };
// const
char* weekdays[7] = { "Elenya", "Anarya", "Isilya", "Alduya", "Menelya", "Valanya", "Earenya" };

// const
char* month_name[17] = { "Narviny\xEB", /* 0 */
    "N\xE9nim\xEB",
    "S\xFAlim\xEB",
    "V\xEDress\xEB",
    "L\xF3tess\xEB",
    "N\xE1ri\xEB",
    "Cermi\xEB",
    "Urim\xEB",
    "Yavanni\xEB",
    "Narqueli\xEB",
    "H\xEDsim\xEB",
    "Ringar\xEB",
    "Month of the Dark Shades",
    "Month of the Shadows",
    "Month of the Long Shadows",
    "Month of the Ancient Darkness",
    "Month of the Great Evil" };

// const
char* where[] = {
    "<used as light>        ",
    "<worn on finger>       ",
    "<worn on finger>       ",
    "<worn around neck>     ",
    "<worn around neck>     ",
    "<worn on body>         ",
    "<worn on head>         ",
    "<worn on legs>         ",
    "<worn on feet>         ",
    "<worn on hands>        ",
    "<worn on arms>         ",
    "<worn as shield>       ",
    "<worn about body>      ",
    "<worn about waist>     ",
    "<worn around wrist>    ",
    "<worn around wrist>    ",
    "<wielded>              ",
    "<held>                 ",
    "<worn on back>         ",
    "<worn on belt>         ",
    "<worn on belt>         ",
    "<worn on belt>         ",
    "<wielded two-handed>   ",
};

char* beornwhere[] = {
    "<used as light>        ",
    "<worn on paw>          ",
    "<worn on paw>          ",
    "<worn around neck>     ",
    "<worn around neck>     ",
    "<worn on body>         ",
    "<worn on head>         ",
    "<worn on hindlegs>     ",
    "<worn on hindfeet>     ",
    "<worn on claws>        ",
    "<worn on forlegs>      ",
    "<worn as shield>       ",
    "<worn about body>      ",
    "<worn about waist>     ",
    "<worn around wrist>    ",
    "<worn around wrist>    ",
    "<wielded>              ",
    "<held>                 ",
    "<worn on back>         ",
    "<worn on belt>         ",
    "<worn on belt>         ",
    "<worn on belt>         ",
    "<wielded two-handed>   ",
};

sh_int encumb_table[MAX_WEAR] = {
    0, 0, 0, 0, 0, 1, /*body*/
    1, /*head*/
    0, /*legs*/
    0, /*feet*/
    2, /*hands*/
    2, /*arms*/
    1, /*shield*/
    1, /*about body*/
    0, /*about waist*/
    0, 0, 1, /*weapon*/
    0, /*held*/
    0, /*back*/
    0, /*belt*/
    0, /*belt*/
    0 /*belt*/
};

sh_int* get_encumb_table() { return encumb_table; }

sh_int leg_encumb_table[MAX_WEAR] = {
    0, 0, 0, 0, 0, 1, /*body*/
    0, /*head*/
    2, /*legs*/
    1, /*feet*/
    0, /*hands*/
    0, /*arms*/
    1, /*shield*/
    0, /*about body*/
    0, /*about waist*/
    0, 0, 0, /*weapon*/
    0, /*held*/
    0, /*back*/
    0, /*belt*/
    0, /*belt*/
    0 /*belt*/
};

sh_int* get_leg_encumb_table() { return leg_encumb_table; }

// const
char* drinks[] = { "water",
    "beer",
    "wine",
    "ale",
    "dark ale",
    "whisky",
    "lemonade",
    "firebreather",
    "local speciality",
    "slime mold juice",
    "milk",
    "tea",
    "coffee",
    "blood",
    "salt water",
    "clear water",
    "\n" };

// const
char* drinknames[] = { "water", "beer", "wine", "ale", "ale", "whisky",
    "lemonade", "firebreather", "local", "juice", "milk", "tea",
    "coffee", "blood", "salt", "water" };

/* effect of drinks on hunger, thirst, and drunkenness -- see values.doc */
int drink_aff[][3] = { { 0, 1, 10 }, { 3, 2, 5 }, { 5, 2, 5 }, { 2, 2, 5 }, { 1, 2, 5 }, { 6, 1, 4 },
    { 0, 1, 8 }, { 10, 0, 0 }, { 3, 3, 3 }, { 0, 4, -8 }, { 0, 3, 6 }, { 0, 1, 6 },
    { 0, 1, 6 }, { 0, 2, -1 }, { 0, 1, -2 }, { 0, 0, 13 } };

// const
char* genders[] = { "neutral", "male", "female" };

// const
char* color_liquid[] = { "clear", "brown", "clear", "brown", "dark", "golden",
    "red", "green", "clear", "light green", "white", "brown",
    "black", "red", "clear", "crystal clear" };

// const
char* fullness[] = { "less than half ", "about half ", "more than half ", "" };

// const
char* item_types[] = {
    "UNDEFINED", "LIGHT", "SCROLL", "WAND", "STAFF", "WEAPON",
    "FIRE WEAPON", "MISSILE", "TREASURE", "ARMOR", "POTION", "WORN",
    "OTHER", "TRASH", "TRAP", "CONTAINER", "NOTE", "LIQUID CONTAINER",
    "KEY", "FOOD", "MONEY", "PEN", "BOAT", "FOUNTAIN",
    "SHIELD", "LEVER", "\n"
};

// const
char* wear_bits[] = { "TAKE", "FINGER", "NECK", "BODY", "HEAD", "LEGS", "FEET",
    "HANDS", "ARMS", "SHIELD", "ABOUT", "WAISTE", "WRIST", "WIELD",
    "HOLD", "THROW", "BACK", "BELT", "\n" };

// const
char* extra_bits[] = { "GLOW", "HUM", "DARK", "BREAKABLE", "EVIL", "INVISIBLE", "MAGIC", "NODROP",
    "BROKEN", "!GOOD", "!EVIL", "!NEUTRAL", "!RENT",
    /* if a sprintbit() ever tries to issue the !DONATE bit on an
     * object, we want to know about it.  it -never- should do this,
     * as no place in the code allows !DONATE except building, in
     * which case builders can, unfortunately, accidentally set the
     * !DONATE bit
     */
    "<OLD !DONATE, REPORT TO IMPS>", "!INVIS", "WILLPOWER", "IMM", "HUMAN",
    "DWARF", "WOODELF", "HOBBIT", "BEORNING", "URUK", "ORC", "ORC_FOLLOWER",
    "LHUTH", "OLOGHAI", "HARADRIM", "STAY_ZONE", "\n" };

// const
char* room_bits[] = { "DARK", "DEATH", "NO_MOB", "INDOORS", "NORIDE", "*PERM*",
    "SHADOWY", "NO_MAGIC", "TUNNEL", "PRIVATE", "GODROOM", "",
    "WATER", "POISON", "SECURITY", "PEACE", "NO_TELEPORT", "HIDE_VNUM",
    "\n" };
char num_of_sector_types = 13;

char* sector_types[] = { "Floor", "City", "Field", "Forest", "Hills",
    "Mountain", "Water", "Water_noswim", "Underwater", "Road",
    "Crack", "Dense_forest", "Swamp", "\n" };

// const
char* exit_bits[] = { "DOOR", "CLOSED", "LOCKED", "NOFLEE", "LOCKED", "NOPICK",
    "ISHEAVY", "NOBREAK", "NOLOOK", "HIDDEN", "BROKEN", "NORIDE",
    "NOBLINK", "LEVER", "NOWALK", "\n" };

// const
//  char	*sector_types[] = {
//     "Inside",
//     "City",
//     "Field",
//     "Forest",
//     "Hills",
//     "Mountains",
//     "Water Swim",
//     "Water NoSwim",
//     "City",
//     "\n"
//  };

// const
char* equipment_types[] = { "Special",
    "Worn on right finger",
    "Worn on left finger",
    "First worn around Neck",
    "Second worn around Neck",
    "Worn on body",
    "Worn on head",
    "Worn on legs",
    "Worn on feet",
    "Worn on hands",
    "Worn on arms",
    "Worn as shield",
    "Worn about body",
    "Worn around waiste",
    "Worn around right wrist",
    "Worn around left wrist",
    "Wielded",
    "Held",
    "Worn on back",
    "Worn on belt",
    "Worn on belt",
    "Worn on belt",
    "\n" };

// const
char* affected_bits[] = { "SENSE",
    "INFRA",
    "SNEAK",
    "HIDE",
    "DET-MAGIC",
    "CHARM",
    "CURSE",
    "SANCT",
    "TWO-HANDED",
    "INVIS",
    "MOONVISION",
    "POISON",
    "SHIELD",
    "BREATHE",
    "GROUP",
    "CONFUSE",
    "SLEEP",
    "BASH",
    "FLYING",
    "DET_INVIS",
    "FEAR",
    "BLIND",
    "FOLLOW",
    "SWIM",
    "HUNT",
    "EVASION",
    "CASTING",
    "WAITWHEEL",
    "",
    "CONCENTR",
    "HAZE",
    "HALLU"
    "\n" };

// const
char* apply_types[] = {
    "NONE", "STR", "DEX", "INT", "WIS", "CON",
    "SEX", "CLASS", "LEVEL", "AGE", "CHAR_WEIGHT", "CHAR_HEIGHT",
    "MANA", "HIT", "MOVE", "GOLD", "EXP", "DODGE",
    "OB", "DAMROLL", "SAVING_SPELL", "WILLPOWER", "REGEN", "VISION",
    "SPEED", "PERCEPTION", "ARMOR", "SPELL", "BITVECTOR", "MANA_REGEN",
    "RESISTANCE", "VULNERAB", "MAUL", "BEND", "PKMAGE", "PKMYSTIC",
    "PKRANGER", "PKWARRIOR", "SPELLPEN", "SPELLPOWER", "\n"
};

// const
char* pc_prof_types[] = { "UNDEFINED", "Magic User", "Cleric", "Ranger", "Warrior", "\n" };

// const
char* pc_races[] = { "God", "Human", "Dwarf", "Wood Elf", "Hobbit", "High Elf", "Beorning",
    "UNDEFINED", "UNDEFINED", "UNDEFINED", "UNDEFINED", "Uruk-Hai", "Harad", "Orc",
    "Easterling", "Uruk-Lhuth", "Undead", "Olog-Hai", "Haradrim", "UNDEFINED",
    "Troll",
    /* Orc is 11, 16 total  ::: here all was changed to lowers, and
   harad human changed to human, for the "look" purposes. */
    "\n" };
char* pc_race_types[] = { "god", "human", "dwarf", "elf", "hobbit", "elf", "beorning", "UNDEFINED",
    "UNDEFINED", "UNDEFINED", "UNDEFINED", "uruk-hai", "human", "orc",
    "easterling", "uruk-lhuth", "undead", "olog-hai", "haradrim", "UNDEFINED",
    "troll",
    /* Orc is 11, 16 total  ::: here all was changed to lowers, and
   harad human changed to human, for the "look" purposes. */
    "\n" };
char* pc_race_keywords[] = { "god", "human", "dwarf", "elf", "hobbit", "elf", "bear", "UNDEFINED",
    "UNDEFINED", "UNDEFINED", "UNDEFINED", "uruk", "human", "orc", "human",
    "uruk", "undead", "olog", "haradrim", "UNDEFINED", "troll",
    /* Orc is 11, 16 total  ::: here all was changed to lowers, and
   harad human changed to human, for the "look" purposes. */
    "\n" };

// const
char* pc_star_types[] = { "God", "*a Human*", "*a Dwarf*", "*an Elf*", "*a Hobbit*", "*an Elf*",
    "*a Bear*", "UNDEFINED", "UNDEFINED", "UNDEFINED", "UNDEFINED",
    "*an Uruk*", "*a Human*", "*an Orc*", "*an Easterling*", "*an Uruk*",
    "*an Undead*", "*an Olog-Hai*", "*a Haradrim*", "UNDEFINED", "*a Troll*",
    /* Orc is 11, 16 total */
    "\n" };

// const
char* pc_named_star_types[] = {
    "%s the God", "*%s the Human*", "*%s the Dwarf*", "*%s the Elf*", "*%s the Hobbit*",
    "*%s the Elf*", "*%s the Beorning*", "%s the UNDEFINED", "%s the UNDEFINED", "%s the UNDEFINED",
    "%s the UNDEFINED", "*%s the Uruk*", "*%s the Human*", "*%s the Orc*", "*%s the Easterling*",
    "*%s the Uruk*", "*%s the Undead*", "*%s the Olog-Hai*", "*%s the Haradrim*",
    "%s the UNDEFINED", "*%s the Troll*",
    /* Orc is 11, 16 total */
    "\n"
};

char* pc_arda_fame_identifier[] = { "", " (Warlord of Arda)", " (Noble Crusader)",
    " (Heroic Knight)", "" };

char* pc_evil_fame_identifier[] = { "", " (Chieftain of Evil)", " (Wicked Commander)",
    " (Sinister Patrol)", "" };

char* pc_arda_fame_keywords[] = { "arda", "warlord", "crusader", "knight", "" };

char* pc_evil_fame_keywords[] = { "evil", "chieftain", "commander", "patrol", "" };

// const
char* npc_prof_types[] = { "Normal", "Undead", "\n" };

// const
char* action_bits[] = {
    "SPEC", "SENTINEL", "SCAVENGER", "ISNPC", "NOBASH", "AGGR", "STAY-ZONE",
    "WIMPY", "STAY-TYPE", "MOUNT", "CAN_SWIM", "MEMORY", "HELPER", "AGGR_EVIL",
    "AGGR_NEUT", "AGGR_GOOD", "BODYGUARD", "WRAITH", "SWITCH", "NORECALC", "ACTIVE",
    "IS_PET", "HUNTER", "ORC_FRIEND", "RACE_GUARD", "ASSISTANT", "GUARDIAN", "\n"
};

// const
char* player_bits[] = { "", "IS_NCHANGED", "FROZEN", "DONTSET", "WRITING", "MAILING",
    "CSH", "SITEOK", "NOSHOUT", "NOTITLE", "DELETED", "LOADRM",
    "!WIZL", "!DEL", "INVST", "RETIRED", "SHAPING", "WR_FINISH",
    "SHADOW", "AFK", "INCOG", "WAS_KITTED", /* used to be: outlaw */
    "", /* watch */
    "\n" };

// const
char* preference_bits[] = { "BRIEF", "COMPACT", "NARR", "!TELL", "MENTAL", "SWIM", "NOTH2",
    "PRMPT", "D_TEXT", "!HASS", "QUEST", "SUMN", "ECHO", "LIGHT",
    "COLOR", "SING", "WIZ", "LOG1", "LOG2", "LOG3", "!AUC",
    "CHAT", "!GTZ", "RMFLG", "SPAM", "AUTOEX", "LATIN1", "SPINNER",
    "SORT1", "SORT2", "!UNUS1", "!UNUS2", "!UNUS3", "\n" };

// const
char* position_types[] = { "Dead", "Mortally wounded", "Incapacitated", "Stunned", "Sleeping",
    "Resting", "Sitting", "Fighting", "Standing", "\n" };

// const
char* connected_types[] = { "Playing",
    "Get name",
    "Confirm name",
    "Get password",
    "Get new PW",
    "Confirm new PW",
    "Select sex",
    "Read MOTD",
    "Main Menu",
    "Get descript.",
    "Select class",
    "Linkless",
    "Changing PW 1",
    "Changing PW 2",
    "Changing PW 3",
    "Disconnecting",

    "Self-Delete 1",
    "Self-Delete 2",
    "Select race",
    "Con_qown1",
    "Con,qown2",
    "Create1",
    "Create2",
    "Linkless",
    "Select latin1",
    "Enable color",
    "\n" };

// const
char* ban_types[] = { "no", "new", "select", "all", "ERROR" };

sh_int square_root[171] = {
    0,
    100,
    141,
    173,
    200,
    223,
    244,
    264,
    282,
    300,
    316,
    331,
    346,
    360,
    374,
    387,
    400,
    412,
    424,
    435,
    447,
    458,
    469,
    479,
    489,
    500,
    509,
    519,
    529,
    538,
    547,
    556,
    565,
    574,
    583,
    591,
    600,
    608,
    616,
    624,
    632,
    640,
    648,
    655,
    663,
    670,
    678,
    685,
    692,
    700,
    707,
    714,
    721,
    728,
    734,
    741,
    748,
    754,
    761,
    768,
    774,
    781,
    787,
    793,
    800,
    806,
    812,
    818,
    824,
    830,
    836,
    842,
    848,
    854,
    860,
    866,
    871,
    877,
    883,
    888,
    894,
    900,
    905,
    911,
    916,
    921,
    927,
    932,
    938,
    943,
    948,
    953,
    959,
    964,
    969,
    974,
    979,
    984,
    989,
    994,
    1000,
    1004,
    1009,
    1014,
    1019,
    1024,
    1029,
    1034,
    1039,
    1044,
    1048,
    1053,
    1058,
    1063,
    1067,
    1072,
    1077,
    1081,
    1086,
    1090,
    1095,
    1100,
    1104,
    1109,
    1113,
    1118,
    1122,
    1126,
    1131,
    1135,
    1140,
    1144,
    1148,
    1153,
    1157,
    1161,
    1166,
    1170,
    1174,
    1178,
    1183,
    1187,
    1191,
    1195,
    1200,
    1204,
    1208,
    1212,
    1216,
    1220,
    1224,
    1228,
    1232,
    1236,
    1240,
    1244,
    1248,
    1252,
    1256,
    1260,
    1264,
    1268,
    1272,
    1276,
    1280,
    1284,
    1288,
    1292,
    1296,
    1300,
    1303,
};

char* prompt_text[] = { "", /*0*/
    "",
    "Lin", /* Make sure these lines are not shifted. */
    "Dgt", /* they are referred by raw numbers,      */
    "Shaping: %d", /* not by defines :(                      */
    "Mobile: %d", /*5*/
    "Object: %d",
    "Zone: %d",
    "Program: %d",
    "Script: %d" };

struct prompt_type prompt_hit[] = {
    { "Dying", 0 }, { "Awful", 100 }, // check in ACMD(do_report) in act_othe.cc
    { "Bloodied", 250 }, // before changing any of these structures
    { "Wounded", 450 }, { "Hurt", 700 }, { "Bruised", 900 }, { "Scratched", 999 }, { "Healthy", 1000 }
};

struct prompt_type health_diagnose[] = { { " is dying.\n\r ", 0 },
    { " is almost dead.\n\r", 100 },
    { " is beaten severely.\n\r", 250 },
    { " has a lot of wounds.\n\r", 450 },
    { " is somewhat hurt.\n\r", 700 },
    { " has some bruises.\n\r", 900 },
    { " has a few scratches.\n\r", 999 },
    { " looks healthy.\n\r", 1000 } };

struct prompt_type prompt_mana[] = { { " S:Empty", 0 }, { " S:Drained", 100 }, { " S:Depleted", 250 },
    { " S:Charged", 500 }, { " S:Powered", 750 }, { " S:Surging", 999 },
    { "", 1000 } };

struct prompt_type prompt_move[] = { { " MV:Exhausted", 0 }, { " MV:Fainting", 50 },
    { " MV:Fatigued", 100 }, { " MV:Tired", 200 },
    { " MV:Weary", 400 }, { "", 1000 } };

struct prompt_type prompt_mount[] = { { " Mount:Exhausted", 0 }, { " Mount:Fainting", 50 },
    { " Mount:Fatigued", 100 }, { " Mount:Tired", 200 },
    { " Mount:Weary", 400 }, { "", 1000 } };

struct prompt_type prompt_spirit[] = { /* Notice that the value means different
                                        thing here, thatn in other prompts */
    { "Unexperienced", 15 }, /* 5 per level, for now */
    { "Conscious", 30 }, { "Devoted", 45 }, { "Ardent", 60 },
    { "Reverend", 75 }, { "Charismatic", 90 }, { "Adept", 105 },
    { "Harmonic", 120 }, { "Saint", 135 }, { "Apostol", 150 },
    { "Master", 165 }, { "Buddha", 180 }
};

char* mobile_program_base[] = {
    "",
    "fp035i`Alas, no one is here.`SP135g`Ah, here you are!`S.vH.vh.=097i`I see you're not "
    "well,`SP135g`I see you're healthy :)`S`magic mi`?U `Let me rest`S ",
    "`command line 2`S",
    "`command line 3`S",
    "`command line 4`S",
    "`command line 5`S",
    "`command line 6`S",
    "`command line 7`S",
    "`command line 8`S",
    "`command line 9`S",
    "`command line 10`S",
    "`command line 11`S",
    "`command line 12`S",
    "`command line 13`S",
    "`command line 14`S",
    "`command line 15`S",
    "`command line 16`S",
    "`I want to become more inteligent!`S",
    "`command line 18`S",
    "`command line 19`S",
    "`command line 20`S",
};

int help_summary_length = 9;

struct help_index_summary help_content[] = {
    { "general", "General information", "text/help_tbl", 0, 0, 0, 0 },
    { "spells", "Abilities available to mages", "text/spel_tbl", 0, 0, 0, 0 },
    { "powers", "Abilities available to the mystic", "text/pray_tbl", 0, 0, 0, 0 },
    { "skills", "Abilities available to both warriors and rangers", "text/skil_tbl", 0, 0, 0, 0 },
    { "specializations", "Becoming an expert in a field of your choice", "text/spec_tbl", 0, 0, 0,
        0 },
    { "wizard", "Information on general immortal commands", "text/wizh_tbl", 0, 0, 1, 0 },
    { "shape", "Information regarding the shaping interface", "text/shap_tbl", 0, 0, 1, 0 },
    { "script", "Information and tutorials for writing scripts", "text/scr_tbl", 0, 0, 1, 0 },
    { "msdp", "MSDP Protocol information", "text/msdp_tbl", 0, 0, 0, 0 }
};

int num_of_ferries = 1;
ferry_boat_type ferry_boat_data[] = { { 2, { 13754, 5600 }, { 13799, 5620 } } };

int num_of_captains = 1;
ferry_captain_type ferry_captain_data[] = {
    { 1121,
        22,
        { 13754, 1402, 1402, 1402, 1402, 1402, 1402, 1402, 1402, 1402, 1402,
            5600, 1402, 1402, 1402, 1402, 1402, 1402, 1402, 1402, 1402, 1402 },
        { 13799, 1401, 1401, 1401, 1401, 1401, 1401, 1401, 1401, 1401, 1401,
            5620, 1401, 1401, 1401, 1401, 1401, 1401, 1401, 1401, 1401, 1401 },
        { 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        3,
        0,
        "With a loud snort, $o prepares to depart.",
        "$n says 'We shall get moving now.'",
        "$o moves in and stops slowly.",
        "$n says 'We shall stop here for a while.'",
        "$o hobbles away.",
        "", //    "$o lurches, and you feel seasick.",
        "$o sails in.",
        "Water splashes at the boards as $o sails along." }
};

char* tactics[] = { "defensive", "careful", "normal", "aggressive", "berserk", "\n" };

char* shooting[] = { "slow", "normal", "fast", "\n" };

char* casting[] = { "slow", "normal", "fast", "\n" };

char* moon_phase[8] = { "new", "new-born", "half-grown", "almost full",
    "full", "slightly wane", "waning", "dying" };

// Perm starting affects for races
long race_affect[] = {
    0, // God
    0, // Human
    0, // Dwarf
    1024, // Wood Elf
    0, // Hobbit
    1024, // High Elf
    2, // Beorning
    0, // !UNUSED!
    0, // !UNUSED!
    0, // !UNUSED!
    0, // !UNUSED!
    2, // Uruk-Hai
    0, // !NPC - Harad!
    2, // Common Orc
    0, // !NPC - Easterling!
    2, // Uruk-Lhuth
    0, // !NPC - Undead!
    2, // Olog-Hai
    1024, // Haradrim
    0, // !UNUSED!
    0, // !NPC - Troll!
    0 // !UNUSED!
};

int max_race_str[MAX_RACES] = {
    22, // God
    22, // Human
    22, // Dwarf
    22, // Wood Elf
    22, // Hobbit
    22, // High Elf
    22, // Beorning
    22, // !UNUSED!
    22, // !UNUSED!
    22, // !UNUSED!
    22, // !UNUSED!
    22, // Uruk-Hai
    22, // !NPC - Harad!
    22, // Common Orc
    22, // !NPC - Easterling!
    22, // Uruk-Lhuth
    22, // !NPC - Undead!
    22, // Olog-Hai
    22, // Haradrim
    22, // !UNUSED!
    22, // !NPC - Troll!
    22 // !UNUSED!
};

int mortal_start_room[MAX_RACES] = {
    1160, // God
    1160, // Human
    1160, // Dwarf
    1170, // Wood Elf
    1160, // Hobbit
    1160, // High Elf
    1184, // Beorning
    1101, // !UNUSED!
    1101, // !UNUSED!
    1101, // !UNUSED!
    1101, // !UNUSED!
    10263, // Uruk-Hai
    10263, // !NPC - Harad!
    14499, // Common Orc
    10263, // !NPC - Easterling!
    13626, // Uruk-Lhuth
    0, // !NPC - Undead!
    1129, // Olog-Hai
    6604, // Haradrim
    0, // !UNUSED!
    0, // !NPC - Troll!
    0 // !UNUSED!
};

int r_mortal_start_room[MAX_RACES];

int mortal_idle_room[MAX_RACES] = {
    1102, // God
    1102, // Human
    1102, // Dwarf
    1102, // Wood Elf
    1102, // Hobbit
    1102, // High Elf
    1102, // Beorning
    1102, // !UNUSED!
    1102, // !UNUSED!
    1102, // !UNUSED!
    1102, // !UNUSED!
    1190, // Uruk-Hai
    10263, // !NPC - Harad!
    1191, // Common Orc
    10263, // !NPC - Easterling!
    1192, // Uruk-Lhuth
    0, // !NPC - Undead!
    1129, // Olog-Hai
    1192, // Haradrim
    0, // !UNUSED!
    0, // !NPC - Troll!
    0 // !UNUSED!
};

int r_mortal_idle_room[MAX_RACES];

int r_bugged_start_room = 1152;

/* Used in do_whois */
char* imm_abbrevs[] = {
    "L. Maia", /* Level 91 */
    " Maia  ", " Maia  ", "G. Maia", " Vala  ", /* Level 95 */
    "G. Vala", " Arata ", "G.Arata", "G.Arata", "  Imp  " /* Level 100 */
};

char* race_abbrevs[MAX_RACES + 40 /*for mob ones*/] = {
    "Imm", "Hum", "Dwf", "WdE", "Hob", "HiE", "Beo", "??", "??", "??", "??",
    "Urk", "Har", "Orc", "Eas", "Lhu", "??", "Olo", "Har", "??", "??", "??"
};

int max_race_align[MAX_RACES] = { 1000, 500, 500, 500, 500, 500, 500, 0, 0, 0, 0,
    -100, -100, -100, -100, -100, 0, -100, -100, 0, 0, 0 };
int min_race_align[MAX_RACES] = { -1000,
    -300, // 1
    -200, // 50
    -200, // 50
    -100, // 100
    -100, // 100
    -100, 0, 0, 0, 0, -500, -500, -500,
    -500, -500, 0, -500, -500, 0, 0, 0 };

const char* specialize_name[game_types::PS_Count] = {
    "nothing", "fire", "cold", "regeneration", "protection",
    "animals", "stealth", "wild fighting", "teleportation", "illusion",
    "lightning", "guardian", "heavy fighting", "light fighting", "defending",
    "archery", "darkness", "arcane", "weapon mastery", "battle magic"
};

char* resistance_name[] = { "UNGROUPED", /* 0 */
    "FIRE", "COLD", "REGEN", "PROT", "ANIMALS", /* 5 */
    "STEALTH",
    "WILD-F", // also resistance to hit-crush
    "TELEPORT", "ILLUSION", "LIGHTNING", /* 10*/
    "MIND", "", "", "", "", /* 15*/
    "\n" };

char* vulnerability_name[] = { "V-UNGROUPED", /* 0 */
    "V-FIRE",
    "V-COLD",
    "V-REGEN",
    "V-PROT",
    "V-ANIMALS", /* 5 */
    "V-STEALTH",
    "V-WILD", // also, vulnerability to hit-crush
    "V-TELEPORT",
    "V-ILLUSION",
    "V-LIGHTNING", /* 10*/
    "V-MIND",
    "",
    "",
    "",
    "", /* 15*/
    "",
    "\n" };

/*
 * Array to hold guardian vnums based
 * on race and guardian type.
 * Guardian type from left to right
 * Aggressive->Defensive->Mystic.
 */

int guardian_mob[MAX_RACES][3] = {
    { 1110, 1110, 1110 }, { 1315, 1316, 1301 }, { 1302, 1310, 1314 }, { 1308, 1309, 1303 },
    { 1304, 1306, 1307 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
    { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 1311, 1317, 1318 },
    { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
    { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
    { 0, 0, 0 }, { 0, 0, 0 }
};

int get_guardian_type(int race_number, const char_data* in_guardian_mob)
{
    extern struct index_data* mob_index;
    if (race_number >= MAX_RACES)
        return INVALID_GUARDIAN;

    int virtual_number = mob_index[in_guardian_mob->nr].virt;
    for (int guardian_type = AGGRESSIVE_GUARDIAN; guardian_type <= MYSTIC_GUARDIAN;
         ++guardian_type) {
        if (guardian_mob[race_number][guardian_type] == virtual_number) {
            return guardian_type;
        }
    }

    return INVALID_GUARDIAN;
}

const skill_data* get_skill_array() { return skills; }

unsigned long stat_ticks_passed = 0;
unsigned long stat_mortals_counter = 0;
unsigned long stat_immortals_counter = 0;
unsigned long stat_whitie_counter = 0;
unsigned long stat_darkie_counter = 0;
unsigned long stat_whitie_legend_counter = 0;
unsigned long stat_darkie_legend_counter = 0;

char* wizlock_default = "The game is closed.  Please try again later.\n\r";

#undef CONSTANTSMARK
