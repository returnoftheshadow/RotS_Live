#ifndef MOB_PROGS_RANGER_PROG_H
#define MOB_PROGS_RANGER_PROG_H

/* Ranger mob program (program 32, mob_ranger_new in spec_pro.cpp): its
 * settings in the mob options. The program itself is in spec_pro.cpp; see
 * docs/systems/ranger-program.md. */

#include <string>
#include <vector>

struct char_data;
struct vendor_problem; /* mob_options.h */

constexpr int PROG_RANGER = 32;

/* Ranger settings, all "<key>=yes|no", all optional (default no):
 *   hide     hides when standing and not fighting, and again after a move
 *   ambush   opens with an ambush instead of a hit
 *   track    follows the tracks of races it is aggressive to
 *   riposte  ripostes parried blows, at skill 75 */
struct ranger_config {
    bool hide = false;
    bool ambush = false;
    bool track = false;
    bool riposte = false;
};

/* Reads the ranger settings out of a mob's options text, appending a
 * vendor_problem for every line it rejects to *problems when non-null.
 * General settings (walkers) are passed over. */
ranger_config parse_ranger_options(const char* text, std::vector<vendor_problem>* problems);

/* The settings are read once per prototype, at boot and when a builder
 * implements the mob, and kept by mob rnum; /save only checks them. */
bool is_ranger_candidate(const struct char_data* proto, int rnum);
void ranger_config_boot(); /* all program-32 prototypes */
void ranger_config_rebuild(int mob_rnum, const struct char_data* builder, bool report = true);
void ranger_config_check(const struct char_data* proto, int mob_vnum, const struct char_data* builder); /* report only */
void ranger_implement_check(int mob_rnum, const struct char_data* builder); /* /imp only: soft reminders */
const ranger_config* ranger_config_for(int mob_rnum); /* nullptr if none */

/* The settings of a running mob: all off for anything that is not a
 * program-32 mob. */
const ranger_config& ranger_settings(const struct char_data* mob);

/* The soft reminders as lines: mob flags program 32 does not act on. */
std::vector<std::string> ranger_flag_warnings(const struct char_data* proto, int mob_vnum);

#endif
