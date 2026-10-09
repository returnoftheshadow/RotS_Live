#include "mob_walker.h"

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "mob_options.h"
#include "script.h"
#include "structs.h"
#include "utils.h"

#include <cstdlib>
#include <map>

using mob_options_detail::split_lines;
using mob_options_detail::trim;

namespace {

/* Digits only, at most 9 of them, so the value always fits an int. */
bool parse_vnum(const std::string& text, int* out)
{
    if (text.empty() || text.size() > 9)
        return false;
    for (char c : text)
        if (c < '0' || c > '9')
            return false;
    *out = atoi(text.c_str());
    return true;
}

const char* type_name(walker_type type)
{
    return type == WALKER_PATH ? "path" : type == WALKER_LOOP ? "loop"
                                                              : "bounded";
}

} // namespace

bool walker_rooms_parse(const std::string& value, std::vector<int>* out)
{
    std::string v = trim(value);
    if (v.empty())
        return false;
    size_t start = 0;
    for (;;) {
        size_t comma = v.find(',', start);
        std::string part = trim(v.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        size_t dash = part.find('-');
        int first, last;
        if (dash == std::string::npos) {
            if (!parse_vnum(part, &first))
                return false;
            last = first;
        } else if (!parse_vnum(trim(part.substr(0, dash)), &first) || !parse_vnum(trim(part.substr(dash + 1)), &last)) {
            return false;
        }
        int step = first <= last ? 1 : -1;
        for (int vnum = first;; vnum += step) {
            if ((int)out->size() >= WALKER_MAX_ROOMS)
                return false;
            out->push_back(vnum);
            if (vnum == last)
                break;
        }
        if (comma == std::string::npos)
            return true;
        start = comma + 1;
    }
}

walker_config parse_walker_options(const char* text, const walker_lookups& lookups, std::vector<walker_problem>* problems)
{
    walker_config config;
    auto error = [&](int line, const std::string& what) {
        if (problems)
            problems->push_back({ line, what, false });
    };
    auto warning = [&](int line, const std::string& what) {
        if (problems)
            problems->push_back({ line, what, true });
    };

    bool saw_any = false, saw_type = false, saw_rooms = false, saw_chance = false, saw_extract = false;
    bool saw_message = false, saw_continue = false, disabled = false;
    int chance_line = 0, extract_line = 0, message_line = 0, continue_line = 0;
    int line_no = 0;
    for (const std::string& raw : split_lines(text)) {
        ++line_no;
        std::string line = trim(raw);
        if (line.empty() || line.compare(0, 2, "//") == 0)
            continue;
        size_t eq = line.find('=');
        std::string key = trim(eq == std::string::npos ? line : line.substr(0, eq));
        std::string value = eq == std::string::npos ? "" : trim(line.substr(eq + 1));

        bool* seen = key == "walker_type" ? &saw_type : key == "walker_move_chance" ? &saw_chance
            : key == "path_complete_extract"                                        ? &saw_extract
            : key == "path_complete_message"                                        ? &saw_message
            : key == "continue_wandering"                                           ? &saw_continue
                                                                                    : nullptr;
        if (!seen && key != "walker_rooms")
            continue; /* not a walker line */
        saw_any = true;
        if (seen && *seen) {
            error(line_no, "duplicate setting - line ignored");
            continue;
        }
        if (seen)
            *seen = true;

        if (key == "walker_type") {
            if (value == "path")
                config.type = WALKER_PATH;
            else if (value == "loop")
                config.type = WALKER_LOOP;
            else if (value == "bounded")
                config.type = WALKER_BOUNDED;
            else {
                error(line_no, "bad walker_type - walker disabled");
                disabled = true;
            }
        } else if (key == "walker_rooms") {
            saw_rooms = true;
            if (!walker_rooms_parse(value, &config.rooms)) {
                error(line_no, "bad room list - walker disabled");
                disabled = true;
            }
        } else if (key == "walker_move_chance") {
            int chance;
            if (parse_vnum(value, &chance) && chance >= 1 && chance <= 100) {
                config.move_chance = chance;
                chance_line = line_no;
            } else {
                error(line_no, "bad value - line ignored");
            }
        } else if (key == "path_complete_message") {
            config.complete_message = value;
            message_line = line_no;
        } else { /* the two yes/no settings */
            bool* flag = key == "path_complete_extract" ? &config.complete_extract : &config.continue_wandering;
            int* at = key == "path_complete_extract" ? &extract_line : &continue_line;
            if (value == "yes" || value == "no") {
                *flag = value == "yes";
                *at = line_no;
            } else {
                error(line_no, "bad value - line ignored");
            }
        }
    }

    if (!saw_any)
        return config;
    if (!disabled && !saw_type) {
        error(0, "walker_type missing - walker disabled");
        disabled = true;
    }
    if (!disabled && (!saw_rooms || config.rooms.empty())) {
        error(0, "walker_rooms missing - walker disabled");
        disabled = true;
    }
    for (size_t i = 0; !disabled && i < config.rooms.size(); ++i) {
        int vnum = config.rooms[i];
        if (config.index.count(vnum)) {
            error(0, "room " + std::to_string(vnum) + " listed twice - walker disabled");
            disabled = true;
        } else if (!lookups.room_exists(vnum)) {
            error(0, "room " + std::to_string(vnum) + " not found - walker disabled");
            disabled = true;
        } else {
            config.index[vnum] = (int)i;
        }
    }
    if (!disabled && config.type == WALKER_LOOP && config.rooms.size() < 2) {
        error(0, "loop needs 2 rooms - walker disabled");
        disabled = true;
    }
    if (disabled) {
        config = walker_config();
        return config;
    }

    /* Settings that do nothing in this mode are warned about and dropped. */
    const std::string mode = type_name(config.type);
    if (config.type != WALKER_PATH) {
        if (extract_line) {
            warning(extract_line, "no effect with " + mode);
            config.complete_extract = false;
        }
        if (continue_line) {
            warning(continue_line, "no effect with " + mode);
            config.continue_wandering = false;
        }
    }
    if (config.type == WALKER_BOUNDED) {
        if (chance_line) {
            warning(chance_line, "no effect with bounded");
            config.move_chance = 100;
        }
        if (message_line) {
            warning(message_line, "no effect with bounded");
            config.complete_message.clear();
        }
    }
    if (config.complete_extract && config.continue_wandering) {
        warning(continue_line, "no effect when extracted");
        config.continue_wandering = false;
    }
    if (config.complete_message.size() > WALKER_MESSAGE_MAX)
        warning(message_line, "longer than 78 columns");

    if (config.type != WALKER_BOUNDED) {
        for (size_t i = 0; i + 1 < config.rooms.size(); ++i)
            if (!lookups.rooms_joined(config.rooms[i], config.rooms[i + 1]))
                warning(0, "no exit from room " + std::to_string(config.rooms[i]) + " to room " + std::to_string(config.rooms[i + 1]));
        if (config.type == WALKER_LOOP && !lookups.rooms_joined(config.rooms.back(), config.rooms.front()))
            warning(0, "no exit from room " + std::to_string(config.rooms.back()) + " to room " + std::to_string(config.rooms.front()));
    }
    return config;
}

bool walker_at_path_end(const walker_config& config, bool finished, int here_vnum)
{
    if (config.type != WALKER_PATH || finished)
        return false;
    auto here = config.index.find(here_vnum);
    return here != config.index.end() && here->second == (int)config.rooms.size() - 1;
}

walker_step walker_decide(const walker_config& config, bool finished, int here_vnum, int target_vnum)
{
    if (config.type == WALKER_NONE || finished)
        return WALKER_STEP_NORMAL;
    auto here = config.index.find(here_vnum);
    if (here == config.index.end())
        return WALKER_STEP_NORMAL; /* off its list: wander until it finds its way back */

    if (config.type == WALKER_BOUNDED)
        return target_vnum < 0 || config.index.count(target_vnum) ? WALKER_STEP_NORMAL : WALKER_STEP_BLOCKED;

    if (target_vnum < 0)
        return WALKER_STEP_BLOCKED;
    bool at_last = here->second == (int)config.rooms.size() - 1;
    if (at_last && config.type == WALKER_PATH)
        return WALKER_STEP_END;
    int next = at_last ? config.rooms.front() : config.rooms[here->second + 1];
    if (target_vnum != next)
        return WALKER_STEP_BLOCKED;
    return at_last ? WALKER_STEP_MOVE_LAP : WALKER_STEP_MOVE;
}

extern struct room_data world;
extern struct char_data* mob_proto;
extern int top_of_mobt;
ACMD(do_move);

namespace {

std::map<int, walker_config> g_walker_configs; /* by mob rnum; only real walkers are stored */

bool exit_is_usable(struct char_data* ch, int door)
{
    return CAN_GO(ch, door) && !IS_SET(world[EXIT(ch, door)->to_room].room_flags, NO_MOB)
        && !IS_SET(world[EXIT(ch, door)->to_room].room_flags, DEATH);
}

bool any_exit_is_usable(struct char_data* ch)
{
    for (int door = 0; door < NUM_OF_DIRS; ++door)
        if (exit_is_usable(ch, door))
            return true;
    return false;
}

/* The STAY flags refuse a walker's step exactly as they refuse a wanderer's. */
bool stay_flags_allow(struct char_data* ch, int door)
{
    int to_room = EXIT(ch, door)->to_room;
    return (!IS_SET(ch->specials2.act, MOB_STAY_ZONE) || world[to_room].zone == world[ch->in_room].zone)
        && (!IS_SET(ch->specials2.act, MOB_STAY_TYPE) || world[to_room].sector_type == world[ch->in_room].sector_type);
}

/* Walkers waiting to be removed (path_complete_extract). A route ends inside
 * the mob's own turn, and the loops driving that turn (mobile_activity,
 * affect_update) still hold the mob, so it is removed by
 * walker_remove_finished() once those loops are done. */
struct pending_removal {
    struct char_data* ch;
    int abs_number;
    std::string message;
    int room; /* the route's last room: where the message goes, wherever the mob is by then */
};
std::vector<pending_removal> g_walker_removals;

void send_route_message(const std::string& message, int room)
{
    if (message.empty() || room < 0)
        return;
    std::string line = message + "\n\r";
    send_to_room(line.c_str(), room);
}

/* Runs the ON_PATH_END script, then sends the message to `room` (the route's
 * last room); with `extract` the mob is queued for removal and its message
 * goes out when it is removed. Returns false if the script itself removed the
 * mob: `ch` must not be touched. */
bool run_route_end(struct char_data* ch, const walker_config& config, bool extract, int room)
{
    int abs_number = ch->abs_number;
    call_trigger(ON_PATH_END, ch, 0, 0);
    if (!char_exists(abs_number) || ch->in_room == NOWHERE) {
        send_route_message(config.complete_message, room);
        return false;
    }
    if (extract)
        g_walker_removals.push_back({ ch, abs_number, config.complete_message, room });
    else
        send_route_message(config.complete_message, room);
    return true;
}

} // namespace

walker_lookups walker_game_lookups()
{
    walker_lookups lookups;
    lookups.room_exists = [](int vnum) { return real_room(vnum) >= 0; };
    lookups.rooms_joined = [](int from_vnum, int to_vnum) {
        int from = real_room(from_vnum), to = real_room(to_vnum);
        if (from < 0 || to < 0)
            return false;
        for (int door = 0; door < NUM_OF_DIRS; ++door)
            if (world[from].dir_option[door] && world[from].dir_option[door]->to_room == to)
                return true;
        return false;
    };
    return lookups;
}

void walker_config_rebuild(int mob_rnum)
{
    g_walker_configs.erase(mob_rnum);
    if (mob_rnum < 0 || mob_rnum > top_of_mobt || !mob_proto[mob_rnum].specials.mob_options)
        return;
    walker_config config = parse_walker_options(mob_proto[mob_rnum].specials.mob_options, walker_game_lookups(), nullptr);
    if (config.type != WALKER_NONE)
        g_walker_configs[mob_rnum] = config;
}

void walker_config_boot()
{
    g_walker_configs.clear();
    for (int rnum = 0; rnum <= top_of_mobt; ++rnum)
        walker_config_rebuild(rnum);
}

const walker_config* walker_config_for(int mob_rnum)
{
    auto it = g_walker_configs.find(mob_rnum);
    return it == g_walker_configs.end() ? nullptr : &it->second;
}

void walker_remove_finished()
{
    std::vector<pending_removal> batch;
    batch.swap(g_walker_removals);
    for (const pending_removal& gone : batch) {
        /* Something else may have removed it first; its path still ended. */
        if (char_exists(gone.abs_number) && gone.ch->in_room != NOWHERE) {
            /* What it wears and carries goes with it: extract_char would
             * leave it all in the room, free to pick up, at every zone reset. */
            for (int slot = 0; slot < MAX_WEAR; ++slot)
                if (gone.ch->equipment[slot])
                    extract_obj(unequip_char(gone.ch, slot));
            while (gone.ch->carrying)
                extract_obj(gone.ch->carrying);
            extract_char(gone.ch);
        }
        send_route_message(gone.message, gone.room);
    }
}

walker_result walker_wander(struct char_data* ch, int door, bool usable)
{
    const walker_config* config = walker_config_for(ch->nr);
    if (!config)
        return WALKER_NOT_HANDLED;
    bool finished = ch->specials.walker_finished != 0;
    int here = world[ch->in_room].number;
    int target = usable ? world[EXIT(ch, door)->to_room].number : -1;
    walker_step step = walker_decide(*config, finished, here, target);
    /* A last room with no way out can never roll a usable exit: any rolled
     * direction then counts as the leave attempt, so the path still ends. */
    if (!usable && walker_at_path_end(*config, finished, here) && !any_exit_is_usable(ch))
        step = WALKER_STEP_END;

    if (step == WALKER_STEP_NORMAL)
        return WALKER_NOT_HANDLED;
    if (step == WALKER_STEP_BLOCKED)
        return WALKER_HANDLED;
    if (step != WALKER_STEP_END && !stay_flags_allow(ch, door))
        return WALKER_HANDLED;
    /* ON_PATH_END does not run for a waiting mob (script.cpp): the end of
     * the path, or of a lap, waits for a turn on which the script can run. */
    if ((step == WALKER_STEP_END || step == WALKER_STEP_MOVE_LAP) && IS_AFFECTED(ch, AFF_WAITING))
        return WALKER_HANDLED;
    if (config->move_chance < 100 && number(1, 100) > config->move_chance)
        return WALKER_HANDLED;

    if (step == WALKER_STEP_END) {
        if (!run_route_end(ch, *config, config->complete_extract, ch->in_room))
            return WALKER_GONE;
        /* Finished, and parked unless it is to wander on. A mob queued for
         * removal is parked too, so it is still here when it is removed. */
        ch->specials.walker_finished = 1;
        if (!config->continue_wandering) {
            SET_BIT(ch->specials2.act, MOB_SENTINEL);
            GET_MAX_MOVE(ch) = 0;
            GET_MOVE(ch) = 0;
        }
        return WALKER_HANDLED;
    }
    /* A route step skips the wanderer's no-two-steps-the-same-way rule. */
    ch->specials.last_direction = door;
    int abs_number = ch->abs_number;
    int last_room = ch->in_room;
    int to_room = EXIT(ch, door)->to_room;
    do_move(ch, "", 0, door + 1, 0);
    /* The step can remove the mob (a script where it arrives, a death). */
    if (!char_exists(abs_number) || ch->in_room == NOWHERE)
        return WALKER_GONE;
    /* A lap ends only once the mob stands in the first room: a refused step
     * runs nothing, and the script's own move cannot cut its DO_WAIT short. */
    if (step == WALKER_STEP_MOVE_LAP && ch->in_room == to_room && !run_route_end(ch, *config, false, last_room))
        return WALKER_GONE;
    return WALKER_HANDLED;
}

std::vector<std::string> walker_problem_lines(int mob_vnum, const std::vector<walker_problem>& problems)
{
    std::vector<std::string> lines;
    for (const walker_problem& problem : problems) {
        std::string line = std::string(problem.warning ? "MOB WARNING" : "MOB ERROR") + ": mobile #" + std::to_string(mob_vnum);
        if (problem.line > 0)
            line += ", options line " + std::to_string(problem.line);
        lines.push_back(line + ": " + problem.text);
    }
    return lines;
}

std::vector<walker_problem> mob_options_unknown_lines(const char* text)
{
    std::vector<walker_problem> unknown;
    int line_no = 0;
    for (const std::string& raw : split_lines(text)) {
        ++line_no;
        std::string line = trim(raw);
        if (line.empty() || line.compare(0, 2, "//") == 0)
            continue;
        size_t eq = line.find('=');
        std::string key = trim(eq == std::string::npos ? line : line.substr(0, eq));
        if (!mob_option_is_general(key))
            unknown.push_back({ line_no, "unknown setting - line ignored", false });
    }
    return unknown;
}

void walker_config_check(const struct char_data* proto, int mob_vnum, bool has_program, struct char_data* builder)
{
    if (!proto || !builder || !proto->specials.mob_options)
        return;
    std::vector<walker_problem> problems;
    parse_walker_options(proto->specials.mob_options, walker_game_lookups(), &problems);
    if (!has_program)
        for (const walker_problem& problem : mob_options_unknown_lines(proto->specials.mob_options))
            problems.push_back(problem);
    for (const std::string& line : walker_problem_lines(mob_vnum, problems)) {
        send_to_char(line.c_str(), builder);
        send_to_char("\n\r", builder);
    }
}
