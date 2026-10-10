#include "mob_csv_extract.h"
#include "char_utils.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "structs.h"
#include "utils.h"
#include <sstream>
#include <sys/stat.h>

extern struct index_data* mob_index;
extern int top_of_mobt;
extern char_data* mob_proto;

struct mob_csv_data {
    std::string name;
    int vnum {};
    std::string aliases;
    std::string title;
    int race {};
    int level {};
    int experience {};
    int alignment {};
    int strength {};
    int intelligence {};
    int will {};
    int dexterity {};
    int constitution {};
    int learning {};
    int health_point {};
    int stamina {};
    int moves {};
    int spirits {};
    int encumbrance {};
    int leg_encumbrance {};
    int perception {};
    int willpower {};
    int coins {};
    int offensive_bonus {};
    int parry {};
    int dodge {};
    int saving_throw {};
    int energy {};
    int energy_regeneration {};
    int damage {};
    int null_speed {};
    int strength_speed {};
    long npc_flags {};
    long aggressive_flags {};
    long will_teach {};
    long script {};
    int special_program_number {};
    int call_mask {};
    long affected_bits {};
    int resistances {};
    int vulnerabilities {};
    int body_type {};

    std::string to_string() const
    {
        std::ostringstream oss;
        oss << "\"" << vnum << "\"" << ",";
        oss << "\"" << name << "\"" << ",";
        oss << "\"" << aliases << "\"" << ",";
        oss << "\"" << title << "\"" << ",";
        oss << "\"" << race << "\"" << ",";
        oss << "\"" << level << "\"" << ",";
        oss << "\"" << experience << "\"" << ",";
        oss << "\"" << alignment << "\"" << ",";
        oss << "\"" << strength << "\"" << ",";
        oss << "\"" << intelligence << "\"" << ",";
        oss << "\"" << will << "\"" << ",";
        oss << "\"" << dexterity << "\"" << ",";
        oss << "\"" << constitution << "\"" << ",";
        oss << "\"" << learning << "\"" << ",";
        oss << "\"" << health_point << "\"" << ",";
        oss << "\"" << stamina << "\"" << ",";
        oss << "\"" << moves << "\"" << ",";
        oss << "\"" << spirits << "\"" << ",";
        oss << "\"" << encumbrance << "\"" << ",";
        oss << "\"" << leg_encumbrance << "\"" << ",";
        oss << "\"" << perception << "\"" << ",";
        oss << "\"" << willpower << "\"" << ",";
        oss << "\"" << coins << "\"" << ",";
        oss << "\"" << offensive_bonus << "\"" << ",";
        oss << "\"" << parry << "\"" << ",";
        oss << "\"" << dodge << "\"" << ",";
        oss << "\"" << saving_throw << "\"" << ",";
        oss << "\"" << energy << "\"" << ",";
        oss << "\"" << energy_regeneration << "\"" << ",";
        oss << "\"" << damage << "\"" << ",";
        oss << "\"" << null_speed << "\"" << ",";
        oss << "\"" << strength_speed << "\"" << ",";
        oss << "\"" << npc_flags << "\"" << ",";
        oss << "\"" << aggressive_flags << "\"" << ",";
        oss << "\"" << will_teach << "\"" << ",";
        oss << "\"" << script << "\"" << ",";
        oss << "\"" << special_program_number << "\"" << ",";
        oss << "\"" << call_mask << "\"" << ",";
        oss << "\"" << affected_bits << "\"" << ",";
        oss << "\"" << resistances << "\"" << ",";
        oss << "\"" << vulnerabilities << "\"" << ",";
        oss << "\"" << body_type << "\"";
        oss << std::endl;
        return oss.str();
    }
};

void mob_csv_extract::create_header(char_data* ch) const
{
    fprintf(file,
        "\"vnum\",\"name\",\"aliases\",\"title\",\"race\","
        "\"level\",\"experience\","
        "\"alignment\",\"strength\",\"intelligence\",\"will\",\"dexterity\","
        "\"constitution\",\"learning\",\"health_point\",\"stamina\",\"moves\",\"spirits\","
        "\"encumbrance\",\"leg_encumbrance\",\"perception\",\"willpower\",\"coins\","
        "\"offensive_bonus\",\"parry\",\"dodge\",\"saving_throw\",\"energy\","
        "\"energy_regeneration\",\"damage\",\"null_speed\",\"strength_speed\","
        "\"npc_flags\",\"aggressive_flags\",\"will_teach\",\"script\","
        "\"special_program_number\",\"call_mask\",\"affected_bits\","
        "\"resistances\",\"vulnerabilities\",\"body_type\"\n");
}

void mob_csv_extract::create_file(char_data* ch)
{
    file = fopen(file_location.c_str(), "w");
    if (!file) {
        send_to_char("Failed to create file.\r\n", ch);
    }

    chmod("../mobs/mob.csv", S_IRWXU | S_IRWXG | S_IRWXO);
}

void mob_csv_extract::close_file(char_data* ch) const
{
    fclose(file);
}

void mob_csv_extract::write_to_file(char_data* ch, const std::string& mob_data) const
{
    fprintf(file, "%s", mob_data.c_str());
}

std::string mob_csv_extract::generate_npc_stat(char_data* mob)
{
    mob_csv_data mob_data;

    mob_data.name = GET_NAME(mob);
    mob_data.vnum = mob_index[mob->nr].virt;
    mob_data.aliases = mob->player.name;
    mob_data.title = mob->player.title;
    mob_data.race = GET_RACE(mob);
    mob_data.level = GET_LEVEL(mob);
    mob_data.experience = GET_EXP(mob);
    mob_data.alignment = GET_ALIGNMENT(mob);
    mob_data.strength = GET_STR(mob);
    mob_data.intelligence = GET_INT(mob);
    mob_data.will = GET_WILL(mob);
    mob_data.dexterity = GET_DEX(mob);
    mob_data.constitution = GET_CON(mob);
    mob_data.learning = GET_LEA(mob);
    mob_data.health_point = utils::get_max_hit(*mob);
    mob_data.stamina = GET_MANA(mob);
    mob_data.moves = utils::get_max_moves(mob);
    mob_data.spirits = utils::get_spirits(mob);
    mob_data.encumbrance = utils::get_encumbrance(*mob);
    mob_data.leg_encumbrance = utils::get_leg_encumbrance(*mob);
    mob_data.perception = utils::get_perception(*mob);
    mob_data.willpower = GET_WILLPOWER(mob);
    mob_data.coins = GET_GOLD(mob);
    mob_data.offensive_bonus = GET_OB(mob);
    mob_data.parry = GET_PARRY(mob);
    mob_data.dodge = GET_DODGE(mob);
    mob_data.saving_throw = mob->specials2.saving_throw;
    mob_data.energy = GET_ENERGY(mob);
    mob_data.energy_regeneration = utils::get_energy_regen(*mob);
    mob_data.damage = GET_DAMAGE(mob);
    mob_data.null_speed = mob->specials.null_speed;
    mob_data.strength_speed = mob->specials.str_speed;
    mob_data.npc_flags = MOB_FLAGS(mob);
    mob_data.aggressive_flags = mob->specials2.pref;
    mob_data.will_teach = mob->specials2.will_teach;
    mob_data.script = mob->specials.script_number;
    mob_data.special_program_number = mob->specials.store_prog_number;
    mob_data.call_mask = CALL_MASK(mob);
    mob_data.affected_bits = mob->specials.affected_by;
    mob_data.resistances = GET_RESISTANCES(mob);
    mob_data.vulnerabilities = GET_VULNERABILITIES(mob);
    mob_data.body_type = GET_BODYTYPE(mob);

    return mob_data.to_string();
}

ACMD(do_mob_csv_extract)
{
    if (GET_LEVEL(ch) < LEVEL_IMPL) {
        send_to_char("You can't do that.\r\n", ch);
        return;
    }

    mob_csv_extract mob_csv;
    mob_csv.create_file(ch);
    if (!mob_csv.file) {
        return;
    }
    mob_csv.create_header(ch);
    for (int i = 0; i < top_of_mobt; i++) {
        if (mob_index[i].virt == 0) {
            continue;
        }

        char_data* mob = read_mobile(mob_index[i].virt, VIRT);
        if (!mob) {
            continue;
        }
        mob->in_room = ch->in_room;

        std::string mob_stat = mob_csv_extract::generate_npc_stat(mob);
        mob_csv.write_to_file(ch, mob_stat);

        mob->in_room = NOWHERE;
        extract_char(mob);
    }

    mob_csv.close_file(ch);
}
