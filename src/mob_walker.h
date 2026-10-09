#ifndef MOB_WALKER_H
#define MOB_WALKER_H

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

struct char_data;

/* Mob walkers: a mob follows a listed route (path, loop) or stays inside a
 * listed area (bounded). Settings are general mob options (mob_options.h):
 * walker_type, walker_rooms, walker_move_chance, path_complete_extract,
 * path_complete_message, continue_wandering. See docs/systems/mob-walkers.md. */

enum walker_type { WALKER_NONE = 0,
    WALKER_PATH,
    WALKER_LOOP,
    WALKER_BOUNDED };

constexpr int WALKER_MAX_ROOMS = 2000;
constexpr size_t WALKER_MESSAGE_MAX = 78;

struct walker_config {
    walker_type type = WALKER_NONE; /* WALKER_NONE = not a walker, or disabled */
    std::vector<int> rooms; /* room vnums in route order */
    std::unordered_map<int, int> index; /* room vnum -> position in rooms */
    int move_chance = 100; /* percent */
    bool complete_extract = false;
    bool continue_wandering = false;
    std::string complete_message;
};

struct walker_problem {
    int line; /* 0 = whole config */
    std::string text;
    bool warning;
};

struct walker_lookups {
    std::function<bool(int)> room_exists; /* by vnum */
    std::function<bool(int, int)> rooms_joined; /* an exit leads from vnum a straight to vnum b */
};

/* Appends the rooms of "1101-1150,1162" to *out. False on bad text or when
 * the list would pass WALKER_MAX_ROOMS. */
bool walker_rooms_parse(const std::string& value, std::vector<int>* out);

/* Reads the walker settings out of a mob's options text, appending a
 * walker_problem for every bad line to *problems when non-null. Lines that
 * are not walker settings are passed over. */
walker_config parse_walker_options(const char* text, const walker_lookups& lookups, std::vector<walker_problem>* problems);

enum walker_step {
    WALKER_STEP_NORMAL, /* not the walker's business: ordinary wandering applies */
    WALKER_STEP_BLOCKED, /* the walker refuses this move */
    WALKER_STEP_MOVE, /* take the route step */
    WALKER_STEP_MOVE_LAP, /* loop: the step into the first room, then the end of the lap */
    WALKER_STEP_END /* path: the route is complete, no step is taken */
};

/* What a walker does with one wander roll. target_vnum is the room the
 * rolled direction leads to, or -1 when that direction has no usable exit.
 * A walker off its list, or one that has finished, wanders normally. */
walker_step walker_decide(const walker_config& config, bool finished, int here_vnum, int target_vnum);

/* True when a path walker that has not finished is standing in its last room. */
bool walker_at_path_end(const walker_config& config, bool finished, int here_vnum);

/* One builder line per problem, at most 78 columns each, no line ending:
 * "MOB ERROR: mobile #<vnum>, options line <n>: <text>" (MOB WARNING for a
 * warning; the line part is left out for a whole-config problem). */
std::vector<std::string> walker_problem_lines(int mob_vnum, const std::vector<walker_problem>& problems);

/* The lines of `text` that are not general settings: on a mob with no
 * options program, lines that nothing reads. */
std::vector<walker_problem> mob_options_unknown_lines(const char* text);

/* ---- In the running game ---- */

enum walker_result {
    WALKER_NOT_HANDLED, /* ordinary wandering applies */
    WALKER_HANDLED, /* the walker rules took this wander roll */
    WALKER_GONE /* its ON_PATH_END script or its step removed the mob: do not touch it */
};

/* The lookups parse_walker_options needs, answered from the loaded world. */
walker_lookups walker_game_lookups();

/* Walker configs are parsed once per mob prototype, at boot and again on
 * /implement, and kept by mob rnum. Nothing is reported from here. */
void walker_config_boot();
void walker_config_rebuild(int mob_rnum);
const walker_config* walker_config_for(int mob_rnum); /* null: not a walker */

/* Called from one_mobile_activity with the direction its wander roll picked
 * (0..NUM_OF_DIRS-1) and whether that exit is usable. */
walker_result walker_wander(struct char_data* ch, int door, bool usable);

/* Removes the walkers whose path ended with path_complete_extract=yes and
 * sends their path_complete_message. Called where no loop holds a mob: after
 * the mob activity pass and after the affect update pass. */
void walker_remove_finished();

/* Mob editor, /save and /implement: sends the walker problems in `proto`'s
 * options to the builder alone. When the mob has no options program
 * (has_program false), lines that no feature knows are reported too. */
void walker_config_check(const struct char_data* proto, int mob_vnum, bool has_program, struct char_data* builder);

#endif
