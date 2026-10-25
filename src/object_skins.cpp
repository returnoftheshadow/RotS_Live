#include "object_skins.h"

#include "db.h"
#include "handler.h"
#include "structs.h"
#include "utils.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern struct index_data* obj_index;
extern struct obj_data* obj_proto;
extern int top_of_objt;

bool obj_is_skin(const obj_data& proto) { return proto.obj_flags.skin_base != 0; }

void copy_skin_stats_from_base(obj_data& skin, const obj_data& base)
{
    obj_flag_data& to = skin.obj_flags;
    const obj_flag_data& from = base.obj_flags;

    to.type_flag = from.type_flag;
    to.extra_flags = from.extra_flags;
    to.wear_flags = from.wear_flags;
    for (int i = 0; i < 5; i++)
        to.value[i] = from.value[i];
    to.weight = from.weight;
    to.cost_per_day = from.cost_per_day;
    to.level = from.level;
    to.rarity = from.rarity;
    to.script_number = from.script_number;
    to.version = from.version;
    if (!(to.skin_own & SKIN_OWN_COST))
        to.cost = from.cost;
    if (!(to.skin_own & SKIN_OWN_MATERIAL))
        to.material = from.material;
    for (int i = 0; i < MAX_OBJ_AFFECT; i++)
        skin.affected[i] = base.affected[i];
}

skin_base_rule check_skin_base(int skin_vnum, int base_vnum)
{
    if (skin_vnum == base_vnum)
        return skin_base_rule::own_base;
    int rnum = real_object(base_vnum);
    if (rnum < 0)
        return skin_base_rule::no_base;
    const obj_data& base = obj_proto[rnum];
    if (base.obj_flags.type_flag == ITEM_KEY)
        return skin_base_rule::base_is_key;
    /* A player writing on a note edits its text in place; a restyle would swap it underneath. */
    if (base.obj_flags.type_flag == ITEM_NOTE)
        return skin_base_rule::base_is_note;
    if (base.obj_flags.skin_base != 0)
        return skin_base_rule::base_is_skin;
    return skin_base_rule::ok;
}

std::string skin_base_problem(int skin_vnum, int base_vnum)
{
    char buf[128];

    switch (check_skin_base(skin_vnum, base_vnum)) {
    case skin_base_rule::ok:
        return "";
    case skin_base_rule::own_base:
        return "an object can't be its own base";
    case skin_base_rule::no_base:
        snprintf(buf, sizeof(buf), "base obj %d not found", base_vnum);
        return buf;
    case skin_base_rule::base_is_key:
        snprintf(buf, sizeof(buf), "base obj %d is a key", base_vnum);
        return buf;
    case skin_base_rule::base_is_note:
        snprintf(buf, sizeof(buf), "base obj %d is a note", base_vnum);
        return buf;
    case skin_base_rule::base_is_skin:
        snprintf(buf, sizeof(buf), "base obj %d is a skin of obj %d", base_vnum,
            obj_proto[real_object(base_vnum)].obj_flags.skin_base);
        return buf;
    }
    return "";
}

int skin_family_rnum(int rnum)
{
    if (rnum < 0)
        return -1;
    int base_vnum = obj_proto[rnum].obj_flags.skin_base;
    if (base_vnum == 0)
        return rnum;
    int base = real_object(base_vnum);
    if (base < 0 || !skin_base_problem(obj_index[rnum].virt, base_vnum).empty())
        return rnum;
    return base;
}

std::vector<int> skin_rnums_of(int base_rnum)
{
    std::vector<int> result;
    if (base_rnum < 0)
        return result;
    int vnum = obj_index[base_rnum].virt;
    for (int i = 0; i <= top_of_objt; i++)
        if (obj_proto[i].obj_flags.skin_base == vnum && i != base_rnum)
            result.push_back(i);
    return result;
}

void apply_object_skins()
{
    char buf[MAX_STRING_LENGTH];
    std::vector<int> demoted;

    /* Judge every skin against the table as loaded, so the result does not depend on order. */
    for (int i = 0; i <= top_of_objt; i++) {
        int base_vnum = obj_proto[i].obj_flags.skin_base;
        if (base_vnum == 0)
            continue;
        std::string problem = skin_base_problem(obj_index[i].virt, base_vnum);
        if (!problem.empty()) {
            snprintf(buf, sizeof(buf), "OBJ ERROR: object #%d: %s - loaded as plain object",
                obj_index[i].virt, problem.c_str());
            mudlog(buf, NRM, LEVEL_IMMORT, TRUE);
            demoted.push_back(i);
        }
    }
    for (int i : demoted) {
        obj_proto[i].obj_flags.skin_base = 0;
        obj_proto[i].obj_flags.skin_own = 0;
    }
    for (int i = 0; i <= top_of_objt; i++) {
        int base_vnum = obj_proto[i].obj_flags.skin_base;
        if (base_vnum != 0)
            copy_skin_stats_from_base(obj_proto[i], obj_proto[real_object(base_vnum)]);
    }
}

void refresh_skins_of_base(int base_rnum)
{
    if (base_rnum < 0)
        return;
    int base_vnum = obj_index[base_rnum].virt;
    for (int rnum : skin_rnums_of(base_rnum))
        if (skin_base_problem(obj_index[rnum].virt, base_vnum).empty())
            copy_skin_stats_from_base(obj_proto[rnum], obj_proto[base_rnum]);
}

int read_object_level_line(FILE* f, int nums[7])
{
    for (int i = 0; i < 7; i++)
        nums[i] = 0;

    int c;
    while ((c = std::getc(f)) != EOF && std::isspace(c))
        ;
    if (c == EOF)
        return 0;
    std::ungetc(c, f);
    if (!std::isdigit(c) && c != '-' && c != '+')
        return 0; /* no level line (a record or E/A token follows): leave it unread */

    char line[256];
    if (!std::fgets(line, sizeof line, f))
        return 0;

    char* p = line;
    int count = 0;
    while (count < 7) {
        char* end;
        long v = std::strtol(p, &end, 10);
        if (end == p)
            break;
        nums[count++] = static_cast<int>(v);
        p = end;
    }
    return count;
}

/* The name a drink container takes from target, built the way filling and emptying do
 * (name_to_drinkcon / name_from_drinkcon): a full prototype's name starts with its liquid, so that
 * word is dropped, and the copy's own liquid goes in front if the copy is full. Returns the
 * prototype's name when that is the answer, otherwise a new string the copy owns. */
static char* drink_name_from(const obj_data& obj, const obj_data& target)
{
    extern char* drinknames[];
    const int liquids = 16; /* entries in drinknames[] */

    char* keys = target.name;
    if (target.obj_flags.value[1] != 0) {
        char* space = std::strchr(keys, ' ');
        if (space)
            keys = space + 1;
    }
    int liquid = obj.obj_flags.value[2];
    if (obj.obj_flags.value[1] == 0 || liquid < 0 || liquid >= liquids)
        return keys == target.name ? target.name : str_dup(keys);

    char* name;
    CREATE(name, char, std::strlen(drinknames[liquid]) + std::strlen(keys) + 2);
    std::sprintf(name, "%s %s", drinknames[liquid], keys);
    return name;
}

restyle_result restyle_object(obj_data* obj, int target_rnum, obj_data** new_obj)
{
    if (target_rnum < 0 || target_rnum > top_of_objt)
        return restyle_result::no_such_object;
    if (obj->item_number < 0)
        return restyle_result::not_a_prototype;
    if (obj->item_number == target_rnum)
        return restyle_result::same_object;
    if (skin_family_rnum(obj->item_number) != skin_family_rnum(target_rnum))
        return restyle_result::not_same_family;

    const obj_data& target = obj_proto[target_rnum];

    obj_index[obj->item_number].number--;
    obj_index[target_rnum].number++;
    obj->item_number = target_rnum;
    /* Text the copy owned is freed as it is replaced. */
    if (obj->obj_flags.type_flag == ITEM_DRINKCON && target.name) {
        char* const drink_name = drink_name_from(*obj, target);
        if (drink_name == target.name) {
            obj->set_borrowed_text(object_text::name, drink_name);
        } else {
            obj->set_owned_text(object_text::name, drink_name);
        }
    } else {
        obj->set_borrowed_text(object_text::name, target.name);
    }
    obj->set_borrowed_text(object_text::short_description, target.short_description);
    obj->set_borrowed_text(object_text::description, target.description);
    obj->set_borrowed_text(object_text::action_description, target.action_description);
    obj->ex_description = target.ex_description;
    obj->obj_flags.cost = target.obj_flags.cost;
    obj->obj_flags.material = target.obj_flags.material;
    obj->obj_flags.skin_base = target.obj_flags.skin_base;
    obj->obj_flags.skin_own = target.obj_flags.skin_own;

    *new_obj = obj;
    return restyle_result::ok;
}
