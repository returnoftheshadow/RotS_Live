/* A room for trigger script tests: one player whose output is captured, mobs and objects made from
 * test prototypes, and a script table the test writes, all put back afterwards. */
#ifndef SCRIPTED_ROOM_FIXTURE_H
#define SCRIPTED_ROOM_FIXTURE_H

#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../owned_character.h"
#include "../protos.h"
#include "../script.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"
#include "test_cleanup.h"
#include "test_random_utils.h"

#include <gtest/gtest.h>

#include <cstring>
#include <new>
#include <deque>
#include <string>
#include <vector>

extern struct obj_data* obj_proto;
extern struct index_data* obj_index;
extern int top_of_objt;
extern struct obj_data* object_list;
extern struct char_data* character_list;
extern struct char_data* combat_list;
extern struct char_data* waiting_list;
extern struct room_data world;
extern int top_of_world;
extern struct descriptor_data* descriptor_list;
extern struct script_head* script_table;
extern int top_of_script_table;

namespace scripted_room {

constexpr int ROOM_VNUM = 5100;
/* The room link_second_room joins to room 0. */
constexpr int SECOND_ROOM_VNUM = 5101;
constexpr int SECOND_ROOM = 1;
/* The script install_script writes; tests that need more than one script add the others. */
constexpr int SCRIPT_VNUM = 9950;
constexpr int OTHER_SCRIPT_VNUM = 9951;
constexpr int THIRD_SCRIPT_VNUM = 9952;
constexpr int STONE_VNUM = 100;
constexpr int BREAD_VNUM = 101;
constexpr int RING_VNUM = 102;
constexpr int SWORD_VNUM = 103;
constexpr int LEVER_VNUM = 104;
constexpr int FLASK_VNUM = 105;
constexpr int BAG_VNUM = 106;
constexpr int BREAD_FILLING = 4;
constexpr int FLASK_DRINKS = 5;
constexpr int SWORD_BULK = 2;
/* The hit points every test mob starts with. */
constexpr int MOB_HIT_POINTS = 500;

/* What a test object's prototype is made of. */
struct prototype_spec {
    // The vnum tests make the object by.
    int virtual_number;
    // The object's keywords, which commands match the argument against.
    const char* keywords;
    // The object's short description, which act() prints for $p.
    const char* short_description;
    // The ITEM_ type the commands check.
    int item_type;
    // ITEM_TAKE and the ITEM_WEAR_ bits wear and wield check.
    int wear_flags;
};

inline const prototype_spec PROTOTYPE_SPECS[] = {
    { STONE_VNUM, "stone grey", "a grey stone", ITEM_OTHER, ITEM_TAKE },
    { BREAD_VNUM, "bread loaf", "a loaf of bread", ITEM_FOOD, ITEM_TAKE },
    { RING_VNUM, "ring gold", "a gold ring", ITEM_WORN, ITEM_TAKE | ITEM_WEAR_FINGER },
    { SWORD_VNUM, "sword short", "a short sword", ITEM_WEAPON, ITEM_TAKE | ITEM_WIELD },
    { LEVER_VNUM, "lever iron", "an iron lever", ITEM_LEVER, 0 },
    { FLASK_VNUM, "flask leather", "a leather flask", ITEM_DRINKCON, ITEM_TAKE },
    { BAG_VNUM, "bag cloth", "a cloth bag", ITEM_CONTAINER, ITEM_TAKE },
};
constexpr int PROTOTYPE_COUNT = sizeof(PROTOTYPE_SPECS) / sizeof(PROTOTYPE_SPECS[0]);

/* The messages the test scripts send, so a test can see that a script ran. */
constexpr const char* SCRIPT_RAN_MESSAGE = "The script ran.";
constexpr const char* CRUMBLE_MESSAGE = "It crumbles to dust.";

/* One command of a test script. */
struct body_line {
    // The SCRIPT_ command.
    int command_type;
    // The command's text (SEND_TO_CHAR's message, DO_SAY's words); null for other commands.
    const char* text;
    // The command's parameters; those not given are 0.
    int parameters[6];
};

/* One trigger block of a test script: <trigger_type> BEGIN <body> END. */
struct trigger_block {
    // The ON_ trigger that runs the block.
    int trigger_type;
    // The commands between BEGIN and END.
    std::vector<body_line> body;
};

/* One script of the test script table. */
struct script_spec {
    // The script's number, which a mob's or object's script_number names.
    int virtual_number;
    // The script's trigger blocks, in order.
    std::vector<trigger_block> blocks;
};

class ScriptedRoomFixture : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_obj_proto = obj_proto;
        m_saved_obj_index = obj_index;
        m_saved_top_of_objt = top_of_objt;
        m_saved_object_list = object_list;
        m_saved_character_list = character_list;
        m_saved_combat_list = combat_list;
        m_saved_waiting_list = waiting_list;
        m_saved_top_of_world = top_of_world;
        m_saved_descriptor_list = descriptor_list;
        m_saved_script_table = script_table;
        m_saved_top_of_script_table = top_of_script_table;
        m_saved_zone_table = zone_table;
        /* Experience for a kill reads the killer's zone. */
        zone_table = m_zones;
        if (room_data::BASE_WORLD == nullptr) {
            world.create_bulk(1);
        }
        for (int room = 0; room < 2; ++room) {
            m_saved_rooms[room].number = world[room].number;
            m_saved_rooms[room].zone = world[room].zone;
            m_saved_rooms[room].light = world[room].light;
            m_saved_rooms[room].contents = world[room].contents;
            m_saved_rooms[room].people = world[room].people;
            for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
                m_saved_rooms[room].exits[direction] = world[room].dir_option[direction];
            }
        }

        descriptor_list = nullptr;
        object_list = nullptr;
        /* extract_char walks these lists; earlier tests in the binary can leave entries. */
        character_list = nullptr;
        combat_list = nullptr;
        waiting_list = nullptr;
        top_of_world = 0;
        world[0].number = ROOM_VNUM;
        world[0].zone = 0;
        world[0].light = 1;
        world[0].contents = nullptr;

        for (int index = 0; index < PROTOTYPE_COUNT; ++index) {
            const prototype_spec& specification = PROTOTYPE_SPECS[index];
            obj_data& prototype = m_prototypes[index];
            clear_object(&prototype);
            prototype.item_number = index;
            std::strncpy(m_keywords[index], specification.keywords, sizeof(m_keywords[index]) - 1);
            std::strncpy(m_short_descriptions[index], specification.short_description,
                sizeof(m_short_descriptions[index]) - 1);
            prototype.name = m_keywords[index];
            prototype.short_description = m_short_descriptions[index];
            prototype.obj_flags.type_flag = specification.item_type;
            prototype.obj_flags.wear_flags = specification.wear_flags;
            prototype.obj_flags.weight = 1;
            m_index[index].virt = specification.virtual_number;
        }
        prototype_for(BREAD_VNUM).obj_flags.value[0] = BREAD_FILLING;
        prototype_for(SWORD_VNUM).obj_flags.value[2] = SWORD_BULK;
        obj_data& flask = prototype_for(FLASK_VNUM);
        flask.obj_flags.value[0] = FLASK_DRINKS * 2; /* capacity */
        flask.obj_flags.value[1] = FLASK_DRINKS; /* what is in it */
        flask.obj_flags.value[2] = 0; /* water */
        flask.obj_flags.weight = FLASK_DRINKS * 2;
        prototype_for(BAG_VNUM).obj_flags.value[0] = 100; /* capacity */
        obj_proto = m_prototypes;
        obj_index = m_index;
        top_of_objt = PROTOTYPE_COUNT - 1;

        clear_char(&m_player, 0);
        /* clear_char zeroes the whole character, the map inside its damage record included; a
         * player gets a working one, as logging in gives it. */
        new (&m_player.damage_details) player_damage_details();
        m_player.player.name = m_player_name;
        m_player.player.race = RACE_HUMAN;
        m_player.player.level = 10;
        m_player.tmpabilities.str = 18;
        m_player.tmpabilities.dex = 18;
        m_player.tmpabilities.intel = 18;
        m_player.specials.position = POSITION_STANDING;
        m_player.specials2.conditions[FULL] = 0;
        m_player.specials2.conditions[THIRST] = 0;
        m_player.specials2.conditions[DRUNK] = 0;
        m_player.in_room = 0;
        m_descriptor.output = m_descriptor.small_outbuf;
        m_descriptor.small_outbuf[0] = '\0';
        m_descriptor.bufspace = SMALL_BUFSIZE - 1;
        m_descriptor.connected = CON_PLYNG;
        m_descriptor.descriptor = 1;
        m_descriptor.character = &m_player;
        m_player.desc = &m_descriptor;
        world[0].people = &m_player;
        m_player.next_in_room = nullptr;
    }

    void TearDown() override
    {
        /* Every character on the list is a test mob still in the game. */
        while (character_list != nullptr) {
            extract_char(character_list);
        }
        clear_test_random_values();
        for (int slot = 0; slot < MAX_WEAR; ++slot) {
            m_player.equipment[slot] = nullptr;
        }
        m_player.carrying = nullptr;
        while (object_list != nullptr) {
            extract_obj(object_list);
        }
        /* The player is not freed, so what clear_char, a fight and any script gave it is released
         * here; its affects go back to the game's pools. */
        while (m_player.affected != nullptr) {
            affect_remove(&m_player, m_player.affected);
        }
        RELEASE(m_player.specials.script_info);
        release_cleared_character(&m_player);
        release_test_descriptor(&m_descriptor);

        for (int room = 0; room < 2; ++room) {
            world[room].number = m_saved_rooms[room].number;
            world[room].zone = m_saved_rooms[room].zone;
            world[room].light = m_saved_rooms[room].light;
            world[room].contents = m_saved_rooms[room].contents;
            world[room].people = m_saved_rooms[room].people;
            for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
                world[room].dir_option[direction] = m_saved_rooms[room].exits[direction];
            }
        }
        obj_proto = m_saved_obj_proto;
        obj_index = m_saved_obj_index;
        top_of_objt = m_saved_top_of_objt;
        object_list = m_saved_object_list;
        character_list = m_saved_character_list;
        combat_list = m_saved_combat_list;
        waiting_list = m_saved_waiting_list;
        top_of_world = m_saved_top_of_world;
        descriptor_list = m_saved_descriptor_list;
        script_table = m_saved_script_table;
        top_of_script_table = m_saved_top_of_script_table;
        zone_table = m_saved_zone_table;
    }

    /* Makes the script table these scripts, replacing any installed before. */
    void install_scripts(const std::vector<script_spec>& scripts)
    {
        m_script_texts.clear();
        m_script_lines.clear();
        m_script_lines.resize(scripts.size());
        m_script_heads.assign(scripts.size(), script_head {});
        for (size_t script = 0; script < scripts.size(); ++script) {
            fill_script_lines(scripts[script], m_script_lines[script]);
            m_script_heads[script].number = scripts[script].virtual_number;
            m_script_heads[script].script = &m_script_lines[script][0];
        }
        script_table = m_script_heads.data();
        top_of_script_table = static_cast<int>(scripts.size()) - 1;
    }

    /* Makes script SCRIPT_VNUM a single trigger block: <trigger> BEGIN <body> END. */
    void install_script(int trigger_type, const std::vector<body_line>& body)
    {
        install_scripts({ { SCRIPT_VNUM, { { trigger_type, body } } } });
    }

    /* A trigger block that tells ch1 it ran and then removes its own object from the game,
     * ending without RETURN_FALSE. */
    void install_self_extracting_script(int trigger_type)
    {
        install_script(trigger_type,
            { { SCRIPT_SEND_TO_CHAR, CRUMBLE_MESSAGE, { SCRIPT_PARAM_CH1 } },
                { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB1 } } });
    }

    /* A trigger block that only tells ch1 it ran. */
    void install_harmless_script(int trigger_type)
    {
        install_script(trigger_type, { { SCRIPT_SEND_TO_CHAR, SCRIPT_RAN_MESSAGE, { SCRIPT_PARAM_CH1 } } });
    }

    /* A trigger block that tells ch1 it ran and returns false. */
    void install_refusing_script(int trigger_type)
    {
        install_script(trigger_type,
            { { SCRIPT_SEND_TO_CHAR, SCRIPT_RAN_MESSAGE, { SCRIPT_PARAM_CH1 } },
                { SCRIPT_RETURN_FALSE, nullptr, {} } });
    }

    /* A fresh object from the prototype with this vnum, running <script_virtual_number> (none for
     * 0), not placed. */
    obj_data* make_object(int virtual_number, int script_virtual_number)
    {
        for (int index = 0; index < PROTOTYPE_COUNT; ++index) {
            if (m_index[index].virt == virtual_number) {
                obj_data* object = read_object(index, REAL);
                object->obj_flags.script_number = script_virtual_number;
                return object;
            }
        }
        ADD_FAILURE() << "no test prototype has vnum " << virtual_number;
        return nullptr;
    }

    /* A fresh object from the prototype with this vnum, running script SCRIPT_VNUM, not placed. */
    obj_data* make_scripted(int virtual_number) { return make_object(virtual_number, SCRIPT_VNUM); }

    /* A mob in <room>, after everyone already there, running <script_virtual_number> (none for
     * 0). The game frees it when it is extracted; TearDown extracts any still in the game. */
    char_data* make_mob(const char* keywords, int script_virtual_number, int room = 0)
    {
        char_data* mob = owned_character(MOB_ISNPC).release();
        SET_BIT(MOB_FLAGS(mob), MOB_ISNPC);
        mob->nr = -1; /* no prototype: free_char releases its strings */
        mob->player.name = str_dup(keywords);
        mob->player.short_descr = str_dup(keywords);
        mob->player.race = RACE_HUMAN;
        mob->player.level = 10;
        mob->tmpabilities.str = 18;
        mob->tmpabilities.dex = 18;
        mob->tmpabilities.intel = 18;
        mob->tmpabilities.con = 20;
        mob->abilities.hit = MOB_HIT_POINTS;
        mob->tmpabilities.hit = MOB_HIT_POINTS;
        mob->abilities.mana = 100;
        mob->tmpabilities.mana = 100;
        mob->specials.position = POSITION_STANDING;
        mob->specials.script_number = script_virtual_number;
        mob->next = character_list;
        character_list = mob;
        /* Registered as read_mobile does, so riding and the other char_exists checks see it. */
        register_npc_char(mob);
        char_to_room(mob, room);
        return mob;
    }

    /* Makes room SECOND_ROOM a lit room joined to room 0 by an exit in every direction, so that
     * a move or a flee from room 0 in any direction leads there; the way back is south. */
    void link_second_room()
    {
        top_of_world = SECOND_ROOM;
        world[SECOND_ROOM].number = SECOND_ROOM_VNUM;
        world[SECOND_ROOM].light = 1;
        world[SECOND_ROOM].zone = 0;
        world[SECOND_ROOM].contents = nullptr;
        world[SECOND_ROOM].people = nullptr;
        for (int direction = 0; direction < NUM_OF_DIRS; ++direction) {
            m_exits[direction] = room_direction_data {};
            m_exits[direction].keyword = m_exit_keyword;
            m_exits[direction].general_description = m_exit_keyword;
            m_exits[direction].key = -1;
            m_exits[direction].to_room = SECOND_ROOM;
            world[0].dir_option[direction] = &m_exits[direction];
            world[SECOND_ROOM].dir_option[direction] = nullptr;
        }
        m_way_back = m_exits[NORTH];
        m_way_back.to_room = 0;
        world[SECOND_ROOM].dir_option[SOUTH] = &m_way_back;
    }

    /* Whether this character is still in the game. Compares addresses only, so it may be given
     * one that has been freed. */
    static bool is_in_game(const char_data* character)
    {
        for (const char_data* listed = character_list; listed; listed = listed->next) {
            if (listed == character) {
                return true;
            }
        }
        return false;
    }

    /* Whether this object is still in the game; as is_in_game. */
    static bool is_in_object_list(const obj_data* object)
    {
        for (const obj_data* listed = object_list; listed; listed = listed->next) {
            if (listed == object) {
                return true;
            }
        }
        return false;
    }

    /* How many objects are on the next_content list that starts with <first>. */
    static int count_contents(const obj_data* first)
    {
        int count = 0;
        for (const obj_data* content = first; content; content = content->next_content) {
            ++count;
        }
        return count;
    }

    /* Runs a command as <actor> typed it. */
    static void run_command_as(char_data* actor,
        void (*command)(char_data*, char*, waiting_type*, int, int), const char* argument,
        int subcommand = 0)
    {
        char line[MAX_INPUT_LENGTH];
        std::strncpy(line, argument, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
        command(actor, line, nullptr, 0, subcommand);
    }

    /* Runs a command as the player typed it. */
    void run_command(void (*command)(char_data*, char*, waiting_type*, int, int),
        const char* argument, int subcommand = 0)
    {
        run_command_as(&m_player, command, argument, subcommand);
    }

    std::string output() const { return std::string(m_descriptor.output); }

    bool output_has(const char* text) const { return output().find(text) != std::string::npos; }

    int count_in_output(const char* text) const
    {
        const std::string captured = output();
        int count = 0;
        for (size_t found = captured.find(text); found != std::string::npos;
             found = captured.find(text, found + 1)) {
            ++count;
        }
        return count;
    }

    obj_data& prototype_for(int virtual_number)
    {
        for (int index = 0; index < PROTOTYPE_COUNT; ++index) {
            if (m_index[index].virt == virtual_number) {
                return m_prototypes[index];
            }
        }
        ADD_FAILURE() << "no test prototype has vnum " << virtual_number;
        return m_prototypes[0];
    }

    // The player who runs the commands; stands in room 0 and is never freed.
    char_data m_player {};
    // Captures what the player is sent, through m_descriptor.output.
    descriptor_data m_descriptor {};

private:
    /* Lays out one script's lines: each block as <trigger> BEGIN <body> END, linked in order. */
    void fill_script_lines(const script_spec& script, std::vector<script_data>& out_lines)
    {
        for (const trigger_block& block : script.blocks) {
            script_data trigger {};
            trigger.command_type = block.trigger_type;
            out_lines.push_back(trigger);
            script_data begin {};
            begin.command_type = SCRIPT_BEGIN;
            out_lines.push_back(begin);
            for (const body_line& line : block.body) {
                script_data command {};
                command.command_type = line.command_type;
                for (int parameter_index = 0; parameter_index < 6; ++parameter_index) {
                    command.param[parameter_index] = line.parameters[parameter_index];
                }
                if (line.text != nullptr) {
                    m_script_texts.emplace_back(line.text);
                    command.text = m_script_texts.back().data();
                }
                out_lines.push_back(command);
            }
            script_data end {};
            end.command_type = SCRIPT_END;
            out_lines.push_back(end);
        }
        for (size_t line = 0; line < out_lines.size(); ++line) {
            out_lines[line].number = static_cast<int>(line) + 1;
            if (line + 1 < out_lines.size()) {
                out_lines[line].next = &out_lines[line + 1];
            }
            if (line > 0) {
                out_lines[line].prev = &out_lines[line - 1];
            }
        }
    }

    // The prototypes read_object copies, indexed by real number.
    obj_data m_prototypes[PROTOTYPE_COUNT] {};
    // The vnum of each prototype, as obj_index holds it.
    index_data m_index[PROTOTYPE_COUNT] {};
    // Writable copies of the prototypes' keywords and short descriptions.
    char m_keywords[PROTOTYPE_COUNT][32] {};
    char m_short_descriptions[PROTOTYPE_COUNT][32] {};
    // The player's name, which act() prints for $n.
    char m_player_name[16] = "Tester";
    // The script table's entries and each one's commands, built by install_scripts.
    std::vector<script_head> m_script_heads;
    std::vector<std::vector<script_data>> m_script_lines;
    // Writable copies of the commands' texts; a deque, so earlier texts never move.
    std::deque<std::string> m_script_texts;

    // The one zone rooms 0 and 1 belong to.
    zone_data m_zones[1] {};
    // The exits link_second_room makes: one from room 0 in each direction, and the way back.
    room_direction_data m_exits[NUM_OF_DIRS] {};
    room_direction_data m_way_back {};
    // The exits' (empty) keyword and description.
    char m_exit_keyword[1] = "";

    /* What SetUp found in rooms 0 and 1. */
    struct saved_room {
        // The fields the tests change.
        int number = 0;
        int zone = 0;
        byte light = 0;
        obj_data* contents = nullptr;
        char_data* people = nullptr;
        room_direction_data* exits[NUM_OF_DIRS] {};
    };

    // The game state SetUp replaces, put back by TearDown.
    obj_data* m_saved_obj_proto = nullptr;
    index_data* m_saved_obj_index = nullptr;
    int m_saved_top_of_objt = 0;
    obj_data* m_saved_object_list = nullptr;
    char_data* m_saved_character_list = nullptr;
    char_data* m_saved_combat_list = nullptr;
    char_data* m_saved_waiting_list = nullptr;
    int m_saved_top_of_world = 0;
    descriptor_data* m_saved_descriptor_list = nullptr;
    script_head* m_saved_script_table = nullptr;
    zone_data* m_saved_zone_table = nullptr;
    int m_saved_top_of_script_table = 0;
    saved_room m_saved_rooms[2] {};
};

} // namespace scripted_room

#endif /* SCRIPTED_ROOM_FIXTURE_H */
