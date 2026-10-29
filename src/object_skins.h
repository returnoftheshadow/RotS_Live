#pragma once

#include <cstdio>
#include <string>
#include <vector>

struct obj_data;

/* An object skin is an object prototype with its own vnum and text whose stats all come from a
 * base prototype (obj_flags.skin_base). skin_own holds SKIN_OWN_* bits for stats the skin keeps. */
inline constexpr int SKIN_OWN_COST = 1;
inline constexpr int SKIN_OWN_MATERIAL = 2;

bool obj_is_skin(const obj_data& proto);

/* Copies every stat (never the text, skin_base or skin_own) from base into skin. Cost and material
 * are kept when the skin's skin_own bits say so. */
void copy_skin_stats_from_base(obj_data& skin, const obj_data& base);

/* The rules for a skin's base, kept here only: boot, implement, restyle and /23 all use them. */
enum class skin_base_rule { ok,
    own_base,
    no_base,
    base_is_key,
    base_is_note,
    base_is_skin };

/* Which rule, if any, base_vnum breaks as the base of skin_vnum. */
skin_base_rule check_skin_base(int skin_vnum, int base_vnum);

/* "" if base_vnum is a valid base for skin_vnum, otherwise the problem (for the logs). */
std::string skin_base_problem(int skin_vnum, int base_vnum);

/* The base's rnum for a valid skin, rnum otherwise, -1 for a negative rnum. */
int skin_family_rnum(int rnum);

/* Rnums of the skins of base_rnum, in rnum order. */
std::vector<int> skin_rnums_of(int base_rnum);

/* Boot pass: copies base stats into every skin, demotes bad skins to plain objects. */
void apply_object_skins();

/* Copies the base's stats into every valid skin of it (a skin whose base is invalid, e.g. a key, is
 * left alone). */
void refresh_skins_of_base(int base_rnum);

/* Reads one object level line (level rarity material script version skin_base skin_own).
 * Skips leading whitespace, consumes the rest of the line only, returns how many integers were
 * read (0-7); the others are set to 0. */
int read_object_level_line(FILE* f, int nums[7]);

enum class restyle_result { ok,
    no_such_object,
    same_object,
    not_same_family,
    not_a_prototype };

/* Changes the live copy obj, in place, into a copy of prototype target_rnum, which must be in the
 * same skin family: it takes the target's vnum (the prototype counts move with it), text, cost,
 * material, skin_base and skin_own, and keeps everything else (place, contents, flags, timer,
 * values, affects, enchants, script, loaded_by, owner, touched). The object stays where it is, so
 * a caller still holding it (e.g. after the object's own trigger) is safe. A drink container's
 * name is rebuilt from the target's the way filling and emptying build it (the target's liquid
 * word dropped, the copy's own liquid in front when full); its old name is left, never freed,
 * since older copies can share it after an implement. On ok *new_obj
 * is set to obj; on every refusal nothing changes and *new_obj is left alone. */
restyle_result restyle_object(obj_data* obj, int target_rnum, obj_data** new_obj);
