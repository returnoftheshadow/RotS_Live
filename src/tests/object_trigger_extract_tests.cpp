/* Object trigger scripts that remove their own object from the game (EXTRACT_OBJ ob1).
 *
 * Once the script has extracted it, the object is freed, so the command that fired the trigger
 * must not touch it again: examine, wear, wield and pull stop there, eat and drink keep what they
 * had already done, and the enter trigger moves on to the room's other objects. A script that
 * leaves its object alone changes nothing. The last tests check call_object_trigger's results
 * directly. */
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../protos.h"
#include "../script.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

extern struct obj_data* obj_proto;
extern struct index_data* obj_index;
extern int top_of_objt;
extern struct obj_data* object_list;
extern struct room_data world;
extern int top_of_world;
extern struct descriptor_data* descriptor_list;
extern struct script_head* script_table;
extern int top_of_script_table;

void clear_char(struct char_data* ch, int mode);
void clear_object(struct obj_data* obj);
void do_eat(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_drink(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_examine(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_pull(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_wear(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_wield(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);

namespace {

constexpr int ROOM_VNUM = 5100;
constexpr int SCRIPT_VNUM = 9950;
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

/* What a test object's prototype is made of. */
struct prototype_spec {
    // The vnum tests make the object by.
    int vnum;
    // The object's keywords, which commands match the argument against.
    const char* keywords;
    // The object's short description, which act() prints for $p.
    const char* short_description;
    // The ITEM_ type the commands check.
    int item_type;
    // ITEM_TAKE and the ITEM_WEAR_ bits wear and wield check.
    int wear_flags;
};

const prototype_spec PROTOTYPE_SPECS[] = {
    { STONE_VNUM, "stone grey", "a grey stone", ITEM_OTHER, ITEM_TAKE },
    { BREAD_VNUM, "bread loaf", "a loaf of bread", ITEM_FOOD, ITEM_TAKE },
    { RING_VNUM, "ring gold", "a gold ring", ITEM_WORN, ITEM_TAKE | ITEM_WEAR_FINGER },
    { SWORD_VNUM, "sword short", "a short sword", ITEM_WEAPON, ITEM_TAKE | ITEM_WIELD },
    { LEVER_VNUM, "lever iron", "an iron lever", ITEM_LEVER, 0 },
    { FLASK_VNUM, "flask leather", "a leather flask", ITEM_DRINKCON, ITEM_TAKE },
    { BAG_VNUM, "bag cloth", "a cloth bag", ITEM_CONTAINER, ITEM_TAKE },
};
constexpr int PROTOTYPE_COUNT = sizeof(PROTOTYPE_SPECS) / sizeof(PROTOTYPE_SPECS[0]);

/* The messages the test scripts send to ch1, so a test can see that its script ran. */
constexpr const char* SCRIPT_RAN_MESSAGE = "The script ran.";
constexpr const char* CRUMBLE_MESSAGE = "It crumbles to dust.";

/* One room, one player whose output is captured, a few prototypes and a one-script table, all
 * put back afterwards. */
class ObjectTriggerTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_obj_proto = obj_proto;
        m_saved_obj_index = obj_index;
        m_saved_top_of_objt = top_of_objt;
        m_saved_object_list = object_list;
        m_saved_top_of_world = top_of_world;
        m_saved_descriptor_list = descriptor_list;
        m_saved_script_table = script_table;
        m_saved_top_of_script_table = top_of_script_table;
        if (room_data::BASE_WORLD == nullptr) {
            world.create_bulk(1);
        }
        m_saved_room_number = world[0].number;
        m_saved_room_light = world[0].light;
        m_saved_room_contents = world[0].contents;
        m_saved_room_people = world[0].people;

        descriptor_list = nullptr;
        object_list = nullptr;
        top_of_world = 0;
        world[0].number = ROOM_VNUM;
        world[0].light = 1;
        world[0].contents = nullptr;

        for (int index = 0; index < PROTOTYPE_COUNT; ++index) {
            const prototype_spec& spec = PROTOTYPE_SPECS[index];
            obj_data& prototype = m_prototypes[index];
            clear_object(&prototype);
            prototype.item_number = index;
            std::strncpy(m_keywords[index], spec.keywords, sizeof(m_keywords[index]) - 1);
            std::strncpy(m_short_descriptions[index], spec.short_description,
                sizeof(m_short_descriptions[index]) - 1);
            prototype.name = m_keywords[index];
            prototype.short_description = m_short_descriptions[index];
            prototype.obj_flags.type_flag = spec.item_type;
            prototype.obj_flags.wear_flags = spec.wear_flags;
            prototype.obj_flags.weight = 1;
            m_index[index].virt = spec.vnum;
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
        m_player.player.name = m_player_name;
        m_player.player.race = RACE_HUMAN;
        m_player.player.level = 10;
        m_player.tmpabilities.str = 18;
        m_player.tmpabilities.dex = 18;
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
        for (int slot = 0; slot < MAX_WEAR; ++slot) {
            m_player.equipment[slot] = nullptr;
        }
        m_player.carrying = nullptr;
        /* free_obj does not release a script's state; the objects a script extracted lose theirs. */
        while (object_list != nullptr) {
            RELEASE(object_list->obj_flags.script_info);
            extract_obj(object_list);
        }

        world[0].number = m_saved_room_number;
        world[0].light = m_saved_room_light;
        world[0].contents = m_saved_room_contents;
        world[0].people = m_saved_room_people;
        obj_proto = m_saved_obj_proto;
        obj_index = m_saved_obj_index;
        top_of_objt = m_saved_top_of_objt;
        object_list = m_saved_object_list;
        top_of_world = m_saved_top_of_world;
        descriptor_list = m_saved_descriptor_list;
        script_table = m_saved_script_table;
        top_of_script_table = m_saved_top_of_script_table;
    }

    /* One command of a test script. */
    struct body_line {
        // The SCRIPT_ command.
        int command_type;
        // SEND_TO_CHAR's message; null for other commands.
        const char* text;
        // The command's parameters; those not given are 0.
        int params[4];
    };

    /* Makes script SCRIPT_VNUM a single trigger block: <trigger> BEGIN <body> END. */
    void install_script(int trigger_type, const std::vector<body_line>& body)
    {
        m_script_texts.clear();
        m_script_texts.reserve(body.size());
        m_script_lines.assign(body.size() + 3, script_data {});
        m_script_lines[0].command_type = trigger_type;
        m_script_lines[1].command_type = SCRIPT_BEGIN;
        for (size_t line = 0; line < body.size(); ++line) {
            script_data& command = m_script_lines[line + 2];
            command.command_type = body[line].command_type;
            for (int param = 0; param < 4; ++param) {
                command.param[param] = body[line].params[param];
            }
            if (body[line].text != nullptr) {
                m_script_texts.emplace_back(body[line].text);
                command.text = m_script_texts.back().data();
            }
        }
        m_script_lines.back().command_type = SCRIPT_END;
        for (size_t line = 0; line < m_script_lines.size(); ++line) {
            m_script_lines[line].number = static_cast<int>(line) + 1;
            if (line + 1 < m_script_lines.size()) {
                m_script_lines[line].next = &m_script_lines[line + 1];
            }
            if (line > 0) {
                m_script_lines[line].prev = &m_script_lines[line - 1];
            }
        }
        m_script_head.number = SCRIPT_VNUM;
        m_script_head.script = &m_script_lines[0];
        script_table = &m_script_head;
        top_of_script_table = 0;
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

    /* A fresh object from the prototype with this vnum, running script SCRIPT_VNUM, not placed. */
    obj_data* make_scripted(int vnum)
    {
        for (int index = 0; index < PROTOTYPE_COUNT; ++index) {
            if (m_index[index].virt == vnum) {
                obj_data* object = read_object(index, REAL);
                object->obj_flags.script_number = SCRIPT_VNUM;
                return object;
            }
        }
        ADD_FAILURE() << "no test prototype has vnum " << vnum;
        return nullptr;
    }

    /* Runs a command as the player typed it. */
    void run_command(void (*command)(char_data*, char*, waiting_type*, int, int),
        const char* argument, int subcmd = 0)
    {
        char line[MAX_INPUT_LENGTH];
        std::strncpy(line, argument, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
        command(&m_player, line, nullptr, 0, subcmd);
    }

    /* Pulls this object, as the interpreter does once it has found it. */
    void pull(obj_data* object)
    {
        waiting_type target {};
        target.targ1.type = TARGET_OBJ;
        target.targ1.ptr.obj = object;
        char empty_argument[1] = "";
        do_pull(&m_player, empty_argument, &target, 0, 0);
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

    obj_data& prototype_for(int vnum)
    {
        for (int index = 0; index < PROTOTYPE_COUNT; ++index) {
            if (m_index[index].virt == vnum) {
                return m_prototypes[index];
            }
        }
        ADD_FAILURE() << "no test prototype has vnum " << vnum;
        return m_prototypes[0];
    }

    // The player who runs the commands; stands in room 0.
    char_data m_player {};
    // Captures what the player is sent, through m_descriptor.output.
    descriptor_data m_descriptor {};

private:
    // The prototypes read_object copies, indexed by real number.
    obj_data m_prototypes[PROTOTYPE_COUNT] {};
    // The vnum of each prototype, as obj_index holds it.
    index_data m_index[PROTOTYPE_COUNT] {};
    // Writable copies of the prototypes' keywords and short descriptions.
    char m_keywords[PROTOTYPE_COUNT][32] {};
    char m_short_descriptions[PROTOTYPE_COUNT][32] {};
    // The player's name, which act() prints for $n.
    char m_player_name[16] = "Tester";
    // The script table's one entry and its commands, built by install_script.
    script_head m_script_head {};
    std::vector<script_data> m_script_lines;
    // Writable copies of the SEND_TO_CHAR messages; reserved up front so text pointers stay valid.
    std::vector<std::string> m_script_texts;

    // The game state SetUp replaces, put back by TearDown.
    obj_data* m_saved_obj_proto = nullptr;
    index_data* m_saved_obj_index = nullptr;
    int m_saved_top_of_objt = 0;
    obj_data* m_saved_object_list = nullptr;
    int m_saved_top_of_world = 0;
    descriptor_data* m_saved_descriptor_list = nullptr;
    script_head* m_saved_script_table = nullptr;
    int m_saved_top_of_script_table = 0;
    int m_saved_room_number = 0;
    byte m_saved_room_light = 0;
    obj_data* m_saved_room_contents = nullptr;
    char_data* m_saved_room_people = nullptr;
};

} // namespace

/* ---- examine (ON_EXAMINE_OBJECT) ---- */

TEST_F(ObjectTriggerTest, ExamineStopsWhenTheScriptExtractsTheObject)
{
    install_self_extracting_script(ON_EXAMINE_OBJECT);
    obj_to_char(make_scripted(STONE_VNUM), &m_player);

    run_command(do_examine, "stone");

    EXPECT_TRUE(output_has(CRUMBLE_MESSAGE));
    EXPECT_FALSE(output_has("You see nothing special")) << "the description step read the stone";
    EXPECT_EQ(nullptr, m_player.carrying);
}

TEST_F(ObjectTriggerTest, ExamineShowsTheDescriptionAfterAScriptThatKeepsTheObject)
{
    install_harmless_script(ON_EXAMINE_OBJECT);
    obj_data* stone = make_scripted(STONE_VNUM);
    obj_to_char(stone, &m_player);

    run_command(do_examine, "stone");

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_TRUE(output_has("You see nothing special"));
    EXPECT_EQ(stone, m_player.carrying);
}

TEST_F(ObjectTriggerTest, ExamineShowsNoDescriptionWhenTheScriptReturnsFalse)
{
    install_refusing_script(ON_EXAMINE_OBJECT);
    obj_to_char(make_scripted(STONE_VNUM), &m_player);

    run_command(do_examine, "stone");

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_FALSE(output_has("You see nothing special"));
}

/* ---- eat and taste (ON_EAT) ---- */

TEST_F(ObjectTriggerTest, EatingFoodWhoseScriptExtractsItFeedsThePlayerOnce)
{
    install_self_extracting_script(ON_EAT);
    obj_to_char(make_scripted(BREAD_VNUM), &m_player);

    run_command(do_eat, "bread", SCMD_EAT);

    EXPECT_TRUE(output_has("You eat a loaf of bread."));
    EXPECT_TRUE(output_has(CRUMBLE_MESSAGE));
    EXPECT_EQ(BREAD_FILLING, GET_COND(&m_player, FULL));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, object_list) << "the bread was extracted once and nothing else";
}

TEST_F(ObjectTriggerTest, TastingFoodWhoseScriptExtractsItStopsThere)
{
    install_self_extracting_script(ON_EAT);
    obj_to_char(make_scripted(BREAD_VNUM), &m_player);

    run_command(do_eat, "bread", SCMD_TASTE);

    EXPECT_TRUE(output_has("You nibble a little bit of a loaf of bread."));
    EXPECT_FALSE(output_has("There's nothing left now."));
    EXPECT_EQ(nullptr, m_player.carrying);
}

TEST_F(ObjectTriggerTest, EatingFoodWithAScriptThatKeepsItStillUsesItUp)
{
    install_harmless_script(ON_EAT);
    obj_to_char(make_scripted(BREAD_VNUM), &m_player);

    run_command(do_eat, "bread", SCMD_EAT);

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(BREAD_FILLING, GET_COND(&m_player, FULL));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, object_list);
}

/* Eat ignores what the script returns: food whose script returns false is eaten all the same. */
TEST_F(ObjectTriggerTest, EatingFoodWhoseScriptReturnsFalseStillUsesItUp)
{
    install_refusing_script(ON_EAT);
    obj_to_char(make_scripted(BREAD_VNUM), &m_player);

    run_command(do_eat, "bread", SCMD_EAT);

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, object_list);
}

TEST_F(ObjectTriggerTest, TastingFoodWithAScriptThatKeepsItTakesOneBite)
{
    install_harmless_script(ON_EAT);
    obj_data* bread = make_scripted(BREAD_VNUM);
    obj_to_char(bread, &m_player);

    run_command(do_eat, "bread", SCMD_TASTE);

    EXPECT_EQ(BREAD_FILLING - 1, bread->obj_flags.value[0]);
    EXPECT_EQ(bread, m_player.carrying);
}

/* ---- drink (ON_DRINK) ---- */

TEST_F(ObjectTriggerTest, DrinkingFromAFlaskWhoseScriptExtractsItStopsThere)
{
    install_self_extracting_script(ON_DRINK);
    obj_to_char(make_scripted(FLASK_VNUM), &m_player);

    run_command(do_drink, "flask", SCMD_DRINK);

    EXPECT_TRUE(output_has("You drink the water."));
    EXPECT_TRUE(output_has(CRUMBLE_MESSAGE));
    EXPECT_EQ(nullptr, m_player.carrying);
}

TEST_F(ObjectTriggerTest, DrinkingFromAFlaskWithAScriptThatKeepsItEmptiesItAsUsual)
{
    install_harmless_script(ON_DRINK);
    obj_data* flask = make_scripted(FLASK_VNUM);
    obj_to_char(flask, &m_player);

    run_command(do_drink, "flask", SCMD_SIP);

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(FLASK_DRINKS - 1, flask->obj_flags.value[1]) << "a sip takes one drink";
}

/* ---- wear and wield (ON_WEAR) ---- */

TEST_F(ObjectTriggerTest, WearingARingWhoseScriptExtractsItStopsThere)
{
    install_self_extracting_script(ON_WEAR);
    obj_to_char(make_scripted(RING_VNUM), &m_player);

    run_command(do_wear, "ring");

    EXPECT_TRUE(output_has(CRUMBLE_MESSAGE));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, m_player.equipment[WEAR_FINGER_R]);
    EXPECT_EQ(nullptr, m_player.equipment[WEAR_FINGER_L]);
}

TEST_F(ObjectTriggerTest, WieldingASwordWhoseScriptExtractsItStopsThere)
{
    install_self_extracting_script(ON_WEAR);
    obj_to_char(make_scripted(SWORD_VNUM), &m_player);

    run_command(do_wield, "sword");

    EXPECT_TRUE(output_has(CRUMBLE_MESSAGE));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, m_player.equipment[WIELD]);
}

TEST_F(ObjectTriggerTest, WearingARingWithAScriptThatKeepsItWearsIt)
{
    install_harmless_script(ON_WEAR);
    obj_data* ring = make_scripted(RING_VNUM);
    obj_to_char(ring, &m_player);

    run_command(do_wear, "ring");

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(ring, m_player.equipment[WEAR_FINGER_R]);
}

TEST_F(ObjectTriggerTest, WearingARingWhoseScriptReturnsFalseLeavesItCarried)
{
    install_refusing_script(ON_WEAR);
    obj_data* ring = make_scripted(RING_VNUM);
    obj_to_char(ring, &m_player);

    run_command(do_wear, "ring");

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_EQ(ring, m_player.carrying);
    EXPECT_EQ(nullptr, m_player.equipment[WEAR_FINGER_R]);
}

/* ---- pull (ON_PULL) ---- */

TEST_F(ObjectTriggerTest, PullingALeverWhoseScriptExtractsItStopsThere)
{
    install_self_extracting_script(ON_PULL);
    obj_data* lever = make_scripted(LEVER_VNUM);
    obj_to_room(lever, 0);

    pull(lever);

    EXPECT_TRUE(output_has(CRUMBLE_MESSAGE));
    EXPECT_FALSE(output_has("seems to be broken"));
    EXPECT_EQ(nullptr, world[0].contents);
}

/* The lever's exit is not set, so a pull that goes ahead finds it broken. */
TEST_F(ObjectTriggerTest, PullingALeverWithAScriptThatKeepsItGoesAhead)
{
    install_harmless_script(ON_PULL);
    obj_data* lever = make_scripted(LEVER_VNUM);
    obj_to_room(lever, 0);

    pull(lever);

    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
    EXPECT_TRUE(output_has("iron lever seems to be broken."));
    EXPECT_EQ(lever, world[0].contents);
}

/* ---- entering a room (ON_ENTER on the room's objects) ---- */

TEST_F(ObjectTriggerTest, EnterTriggersEveryObjectWhenEachExtractsItself)
{
    install_self_extracting_script(ON_ENTER);
    obj_to_room(make_scripted(STONE_VNUM), 0);
    obj_to_room(make_scripted(STONE_VNUM), 0);
    obj_to_room(make_scripted(STONE_VNUM), 0);

    call_trigger(ON_ENTER, &world[0], &m_player, 0);

    EXPECT_EQ(3, count_in_output(CRUMBLE_MESSAGE));
    EXPECT_EQ(nullptr, world[0].contents);
}

TEST_F(ObjectTriggerTest, EnterTriggersEveryObjectWhoseScriptKeepsIt)
{
    install_harmless_script(ON_ENTER);
    obj_to_room(make_scripted(STONE_VNUM), 0);
    obj_to_room(make_scripted(STONE_VNUM), 0);

    call_trigger(ON_ENTER, &world[0], &m_player, 0);

    EXPECT_EQ(2, count_in_output(SCRIPT_RAN_MESSAGE));
}

/* ---- call_object_trigger and call_trigger ---- */

TEST_F(ObjectTriggerTest, ScriptThatKeepsItsObjectProceeds)
{
    install_harmless_script(ON_EXAMINE_OBJECT);
    obj_data* stone = make_scripted(STONE_VNUM);

    EXPECT_EQ(object_trigger_result::proceed,
        call_object_trigger(ON_EXAMINE_OBJECT, stone, &m_player));
    EXPECT_TRUE(output_has(SCRIPT_RAN_MESSAGE));
}

TEST_F(ObjectTriggerTest, ScriptThatReturnsFalseIsRefused)
{
    install_refusing_script(ON_EXAMINE_OBJECT);
    obj_data* stone = make_scripted(STONE_VNUM);

    EXPECT_EQ(object_trigger_result::refused,
        call_object_trigger(ON_EXAMINE_OBJECT, stone, &m_player));
}

/* The script extracts the bag the stone is in, which frees the stone with it. */
TEST_F(ObjectTriggerTest, ObjectFreedWithItsContainerIsReportedExtracted)
{
    install_script(ON_EXAMINE_OBJECT,
        { { SCRIPT_ASSIGN_INV, nullptr,
              { BAG_VNUM, SCRIPT_PARAM_OB2, SCRIPT_PARAM_CH1, SCRIPT_PARAM_INT1 } },
            { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB2 } } });
    obj_data* bag = make_scripted(BAG_VNUM);
    bag->obj_flags.script_number = 0;
    obj_data* stone = make_scripted(STONE_VNUM);
    obj_to_obj(stone, bag);
    obj_to_char(bag, &m_player);

    EXPECT_EQ(object_trigger_result::object_extracted,
        call_object_trigger(ON_EXAMINE_OBJECT, stone, &m_player));
    EXPECT_EQ(nullptr, m_player.carrying);
    EXPECT_EQ(nullptr, object_list);
}

TEST_F(ObjectTriggerTest, ObjectWithoutTheTriggersScriptProceeds)
{
    install_self_extracting_script(ON_EAT);
    obj_data* stone = make_scripted(STONE_VNUM);

    EXPECT_EQ(object_trigger_result::proceed,
        call_object_trigger(ON_EXAMINE_OBJECT, stone, &m_player));
    EXPECT_EQ(stone, object_list);
}

/* An object's own triggers run only through call_object_trigger. */
TEST_F(ObjectTriggerTest, CallTriggerRunsNoObjectTrigger)
{
    install_self_extracting_script(ON_WEAR);
    obj_data* ring = make_scripted(RING_VNUM);

    EXPECT_EQ(1, call_trigger(ON_WEAR, ring, &m_player, 0));
    EXPECT_FALSE(output_has(CRUMBLE_MESSAGE));
    EXPECT_EQ(ring, object_list);
}
