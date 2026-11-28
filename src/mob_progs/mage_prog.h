#ifndef MOB_PROGS_MAGE_PROG_H
#define MOB_PROGS_MAGE_PROG_H

/* Mage mob program (program 31, mob_magic_user_spec in spec_pro.cpp): its
 * settings in the mob options. The program itself is in spec_pro.cpp; see
 * docs/systems/mage-program.md. */

#include <string>
#include <vector>

struct char_data;
struct vendor_problem; /* mob_options.h */

constexpr int PROG_MAGE = 31;

/* Mage settings, all "<key>=yes|no", all optional (default no). The types
 * pick the combat spell lists; several types add their lists together.
 *   fire       fire spells
 *   lightning  lightning spells
 *   cold       cold spells
 *   dark       dark spells
 *   lhuth      lhuth spells; never casts poison
 *   mixed      fire and lightning spells
 *   conjurer   regeneration and curing out of a fight, confuse and poison in one
 *   shield     breaks off the fight to cast shield when interrupted too often
 *   terror     may cast terror in its lowest health band */
struct mage_config {
    bool fire = false;
    bool lightning = false;
    bool cold = false;
    bool dark = false;
    bool lhuth = false;
    bool mixed = false;
    bool conjurer = false;
    bool shield = false;
    bool terror = false;

    bool has_type() const { return fire || lightning || cold || dark || lhuth || mixed; }
};

/* The six types: each one's key and flag, in the order of the type numbers
 * and spell lists in spec_pro.cpp (fire_mage .. default_mage). */
struct mage_type_key {
    const char* key;
    bool mage_config::*flag;
};
constexpr mage_type_key mage_types_table[] = {
    { "fire", &mage_config::fire },
    { "lightning", &mage_config::lightning },
    { "cold", &mage_config::cold },
    { "dark", &mage_config::dark },
    { "lhuth", &mage_config::lhuth },
    { "mixed", &mage_config::mixed },
};
constexpr int mage_type_count = sizeof(mage_types_table) / sizeof(mage_types_table[0]);

/* Reads the mage settings out of a mob's options text, appending a
 * vendor_problem for every line it rejects to *problems when non-null.
 * General settings (walkers) are passed over. */
mage_config parse_mage_options(const char* text, std::vector<vendor_problem>* problems);

/* The settings are read once per prototype, at boot and when a builder
 * implements the mob, and kept by mob rnum; /save only checks them. */
bool is_mage_candidate(const struct char_data* proto, int rnum);
void mage_config_boot(); /* all program-31 prototypes */
void mage_config_rebuild(int mob_rnum, const struct char_data* builder, bool report = true);
void mage_config_check(const struct char_data* proto, int mob_vnum, const struct char_data* builder); /* report only */
void mage_implement_check(int mob_rnum, const struct char_data* builder); /* /imp only: soft reminders */
const mage_config* mage_config_for(int mob_rnum); /* nullptr if none */

/* The settings of a running mob: all off for anything that is not a
 * program-31 mob. */
const mage_config& mage_settings(const struct char_data* mob);

/* The soft reminders as lines: a mage with no type casts no type spells. */
std::vector<std::string> mage_setting_warnings(const char* options, int mob_vnum);

#endif
