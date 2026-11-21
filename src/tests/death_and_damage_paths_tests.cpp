/* Putting gear on and taking it off, dying of negative regeneration, area spells and an NPC
 * olog-hai's skills, when a blow, a death or a script set off on the way removes one of the
 * characters involved: nothing touches the removed one, and what stays behaves as it does
 * without the removal.
 *
 * Poison here comes from an item that gives its wearer the poison flag (APPLY_BITVECTOR), which
 * hurts the wearer as the item goes on and comes off. A test that needs a roll to go one way
 * queues it with push_test_random_value. */
#include "scripted_room_fixture.h"

#include "../act_move.h"
#include "../big_brother.h"
#include "../char_utils.h"
#include "../comm.h" /* complete_delay */
#include "../mob_walker.h"
#include "../skill_timer.h"
#include "../spells.h"

#include <string>

ACMD(do_wear);
ACMD(do_say);
ACMD(do_wield);
ACMD(do_remove);
ACMD(do_cleave);
ACMD(do_knock);
ACMD(do_order);
void assign_command_pointers(void);
void raw_kill(char_data* dead_man, const char_data* killer, int attack_type);
void fast_update();
SPECIAL(ghoul);
SPECIAL(vampire_huntress);
SPECIAL(thuringwethil);
void affect_update_room(struct room_data* room);
bool is_corpse(const obj_data* container);

namespace olog_hai {
int get_base_skill_damage(char_data& olog_hai, int probability);
}

extern struct index_data* mob_index;
extern struct char_data* mob_proto;
extern int top_of_mobt;

using namespace scripted_room;

namespace {

/* The poison bit, as the APPLY_BITVECTOR modifier that gives it (the bit's position). */
constexpr int POISON_BIT_POSITION = 11;
/* What the poison deals as an item goes on or comes off. */
constexpr int POISON_DAMAGE = 5;
/* Few enough hit points that the poison kills. */
constexpr int FEWER_HIT_POINTS_THAN_THE_POISON = 3;
/* A weapon at least this bulky is wielded in two hands where it can be. */
constexpr int TWO_HANDED_BULK = 5;
/* The ghoul the ghoul special raises, which sinks back into the ground when idle. */
constexpr int RISEN_GHOUL_VNUM = 15300;
// The tower room both of Thuringwethil's forms fly back to when they leave.
constexpr int VAMPIRE_TOWER_VNUM = 15379;
// Thuringwethil at this many hit points or fewer is beaten and leaves the game.
constexpr int THURINGWETHIL_BEATEN_HIT_POINTS = 500;
// A roll that number(from, to) turns into its lowest result.
constexpr double LOWEST_ROLL = 0.0;
// Thuringwethil's human form, which the Vampire Huntress becomes in her tower, and the first item
// that form puts on there.
constexpr int THURINGWETHIL_HUMAN_VNUM = 15302;
constexpr int HUMAN_FORM_FIRST_ITEM_VNUM = 6338;
// Hit points of a Vampire Huntress fleeing a fight: her human form gets three times as many, fewer
// than the poison deals.
constexpr int HUNTRESS_HIT_POINTS_HER_HUMAN_FORM_CANNOT_SURVIVE = 1;
// The cell the Vampire Huntress takes a prisoner of the player's race to.
constexpr int CELL_FOR_HUMANS_VNUM = 15398;
// The rolls of number(0, 2) on which the Vampire Huntress, coming upon a player, carries them off
// or attacks them.
constexpr double ABDUCTION_ROLL = 0.5;
constexpr double ATTACK_ROLL = 0.9;
// The to-hit roll of number(1, 35) that always lands a blow.
constexpr double SURE_HIT_ROLL = 0.999;
// How long a script's pause lasts: long enough that only the Huntress's own next delay ends it.
constexpr int SCRIPT_PAUSE_PULSES = 5;
// A door's key number when it has no keyhole.
constexpr int NO_KEYHOLE = -1;
// A regeneration that drains far more hit points than a mob has in one fast update.
constexpr int DRAINING_REGENERATION = -10 * MOB_HIT_POINTS;
// How long a room spell the tests cast lasts, in hours, and how hard the blaze burns.
constexpr int ROOM_SPELL_HOURS = 5;
constexpr int BLAZE_STRENGTH = 10;
// affect_update_room's rolls: a mist roll (number(1, 100)) of 75 or more would keep a mist where
// it is, and a room spell's roll (number(0, 12)) of 0 makes it strike an occupant.
constexpr double MIST_STAYS_ROLL = 0.99;
constexpr double ROOM_SPELL_STRIKES_ROLL = 0.0;
// A mob level at which casting a shout of pain never fumbles.
constexpr int UNFUMBLING_CASTER_LEVEL = 50;

/* The killer remove_the_killer_on_death removes; null once it has. */
char_data* killer_to_remove = nullptr;

/* A death procedure that removes killer_to_remove and lets the death go on. Written out rather
 * than through SPECIAL, as is kill_whoever_knocks, so it names only the parameters it reads. */
int remove_the_killer_on_death(char_data*, char_data*, int, char*, int callflag, waiting_type*)
{
    if (callflag == SPECIAL_DEATH && killer_to_remove != nullptr) {
        char_data* const killer = killer_to_remove;
        killer_to_remove = nullptr;
        extract_char(killer);
    }
    return FALSE;
}

/* A doorkeeper's procedure that kills whoever knocks on its door. */
int kill_whoever_knocks(
    char_data* host, char_data* actor, int command_number, char*, int, waiting_type*)
{
    if (command_number == CMD_KNOCK && actor != host) {
        raw_kill(actor, host, TYPE_HIT);
        return TRUE;
    }
    return FALSE;
}

class DeathAndDamagePathsTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        /* Made at boot; damage and the skills ask them. Making them again keeps the ones there
         * are. */
        game_rules::big_brother::create(weather_info, &world);
        game_timer::skill_timer::create(weather_info, &world);
        m_saved_room_flags = world[0].room_flags;
        /* No crack opens under an earthquake indoors (it would add a room). */
        world[0].room_flags = INDOORS;
        /* A test mob has no prototype (nr -1), and a corpse is marked with its mob's vnum from
         * mob_index[nr]: index -1 is given an entry. */
        m_saved_mob_index = mob_index;
        mob_index = &m_mob_index[1];
        /* Entry 0 of the mob table is the ghoul the ghoul special raises (make_into_risen_ghoul). */
        m_mob_index[1].virt = RISEN_GHOUL_VNUM;
        m_saved_mob_proto = mob_proto;
        m_saved_top_of_mobt = top_of_mobt;
        mob_proto = &m_risen_ghoul_prototype;
        top_of_mobt = 0;
        m_saved_sunlight = weather_info.sunlight;
        m_saved_second_room_name = world[SECOND_ROOM].name;
        m_saved_second_room_description = world[SECOND_ROOM].description;
    }

    void TearDown() override
    {
        world[0].room_flags = m_saved_room_flags;
        weather_info.sunlight = m_saved_sunlight;
        world[SECOND_ROOM].name = m_saved_second_room_name;
        world[SECOND_ROOM].description = m_saved_second_room_description;
        ScriptedRoomFixture::TearDown();
        mob_index = m_saved_mob_index;
        mob_proto = m_saved_mob_proto;
        top_of_mobt = m_saved_top_of_mobt;
    }

    /* Makes the ring and the sword poison whoever wears them. */
    void make_ring_and_sword_poisonous()
    {
        for (int virtual_number : { RING_VNUM, SWORD_VNUM }) {
            obj_affected_type& affect = prototype_for(virtual_number).affected[0];
            affect.location = APPLY_BITVECTOR;
            affect.modifier = POISON_BIT_POSITION;
        }
    }

    /* Takes the player out of the room, so that area spells and skills leave it alone. */
    void take_player_out_of_the_room() { char_from_room(&m_player); }

    /* The objects lying in room 0 that are corpses. */
    static int count_corpses_in_room()
    {
        int corpses = 0;
        for (const obj_data* content = world[0].contents; content; content = content->next_content) {
            if (is_corpse(content)) {
                ++corpses;
            }
        }
        return corpses;
    }

    /* Whether <object> lies on the floor of room 0. Compares addresses only, so a freed object
     * is never read. */
    static bool is_on_the_floor(const obj_data* object)
    {
        return is_on_content_list(world[0].contents, object);
    }

    /* Makes script SCRIPT_VNUM say "vanish" when its mob is hurt, and script OTHER_SCRIPT_VNUM
     * remove its own mob when it hears that word: a blow at the first removes the second. */
    void install_hurt_then_vanish_scripts()
    {
        install_scripts({ { SCRIPT_VNUM,
                              { { ON_DAMAGE, { { SCRIPT_DO_SAY, "vanish", { SCRIPT_PARAM_CH1 } } } } } },
            { OTHER_SCRIPT_VNUM,
                { { ON_HEAR_SAY,
                    { { SCRIPT_IF_STR_CONTAINS, "VANISH", { SCRIPT_PARAM_STR1 } },
                        { SCRIPT_BEGIN, nullptr, {} },
                        { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } },
                        { SCRIPT_END, nullptr, {} } } } } } });
    }

    /* Makes test mob <mob> a ghoul the ghoul special raised, a copy of mob table entry 0. As
     * read_mobile's copies do, it shares the entry's keywords and proficiencies, which the game
     * leaves alone when it removes the mob. */
    void make_into_risen_ghoul(char_data* mob)
    {
        RELEASE(mob->player.name);
        RELEASE(mob->player.short_descr);
        RELEASE(mob->profs);
        mob->player.name = m_risen_ghoul_keywords;
        mob->player.short_descr = m_risen_ghoul_keywords;
        mob->profs = &m_risen_ghoul_proficiencies;
        mob->nr = 0;
    }

    /* A mob wearing the poisonous ring, which hurt it as it went on; it then runs
     * <script_virtual_number> (none for 0). */
    char_data* make_mob_wearing_poisonous_ring(const char* keywords, int script_virtual_number)
    {
        char_data* wearer = make_mob(keywords, 0);
        obj_data* ring = make_object(RING_VNUM, 0);
        obj_to_char(ring, wearer);
        run_command_as(wearer, do_wear, "ring");
        EXPECT_EQ(ring, wearer->equipment[WEAR_FINGER_R]);
        wearer->specials.script_number = script_virtual_number;
        return wearer;
    }

    /* The real number of the test prototype with this vnum, as zone commands name objects. */
    static int real_number_of(int virtual_number)
    {
        for (int index = 0; index < PROTOTYPE_COUNT; ++index) {
            if (PROTOTYPE_SPECS[index].virtual_number == virtual_number) {
                return index;
            }
        }
        ADD_FAILURE() << "no test prototype has vnum " << virtual_number;
        return 0;
    }

    /* How many objects in the game were made from the prototype with this vnum. */
    static int count_objects_of(int virtual_number)
    {
        int count = 0;
        for (const obj_data* listed = object_list; listed; listed = listed->next) {
            if (listed->item_number == real_number_of(virtual_number)) {
                ++count;
            }
        }
        return count;
    }

    /* Makes zone 0 three commands: take the first test mob in room 0, put a ring on it, and put
     * a stone in the last object loaded. */
    void install_ring_then_stone_reset()
    {
        m_reset_commands[0] = reset_com {};
        m_reset_commands[0].command = 'L';
        m_reset_commands[0].arg1 = 0; /* sets the last mob */
        m_reset_commands[0].arg2 = 0; /* room 0 */
        m_reset_commands[0].arg3 = -1; /* a test mob's prototype number */
        m_reset_commands[0].arg4 = 1; /* the first one */
        m_reset_commands[1] = reset_com {};
        m_reset_commands[1].command = 'E';
        m_reset_commands[1].arg1 = real_number_of(RING_VNUM);
        m_reset_commands[1].arg2 = WEAR_FINGER_R;
        m_reset_commands[1].arg4 = 100; /* always */
        m_reset_commands[2] = reset_com {};
        m_reset_commands[2].command = 'P';
        m_reset_commands[2].arg1 = NOWHERE; /* into the last object loaded */
        m_reset_commands[2].arg2 = real_number_of(STONE_VNUM);
        m_reset_commands[2].arg3 = -1;
        m_reset_commands[2].arg5 = 100; /* always */
        zone_table[0].cmd = m_reset_commands;
        zone_table[0].cmdno = RESET_COMMAND_COUNT;
    }

    // Makes mob table entry 0 Thuringwethil's human form, as read_mobile copies it: standing, with
    // the risen ghoul's keywords and proficiencies.
    void make_mob_table_her_human_form()
    {
        m_mob_index[1].virt = THURINGWETHIL_HUMAN_VNUM;
        char_data& prototype = m_risen_ghoul_prototype;
        prototype.nr = 0;
        prototype.specials2.act = MOB_ISNPC;
        prototype.player.name = m_risen_ghoul_keywords;
        prototype.player.short_descr = m_risen_ghoul_keywords;
        prototype.profs = &m_risen_ghoul_proficiencies;
        prototype.specials.position = POSITION_STANDING;
    }

    // Makes the bag's prototype the first item the Huntress's human form puts on: a ring that
    // poisons its wearer. The bag is the last test prototype, so its new vnum keeps the object
    // table in order.
    void make_her_first_item_a_poisonous_ring()
    {
        obj_data& prototype = prototype_for(BAG_VNUM);
        prototype.obj_flags.type_flag = ITEM_WORN;
        prototype.obj_flags.wear_flags = ITEM_TAKE | ITEM_WEAR_FINGER;
        prototype.obj_flags.value[0] = 0;
        prototype.affected[0].location = APPLY_BITVECTOR;
        prototype.affected[0].modifier = POISON_BIT_POSITION;
        obj_index[real_number_of(BAG_VNUM)].virt = HUMAN_FORM_FIRST_ITEM_VNUM;
    }

    // Makes room 0 Thuringwethil's tower, where the Vampire Huntress flies at sunrise and comes
    // upon the player, and SECOND_ROOM the cell she carries a human prisoner off to. The cell's
    // north door leads up to the tower, and the tower's south exit is that door's far side.
    // Returns the Huntress, waiting in the cell.
    char_data* ready_huntress_at_sunrise()
    {
        link_second_room();
        world[0].number = VAMPIRE_TOWER_VNUM;
        world[SECOND_ROOM].number = CELL_FOR_HUMANS_VNUM;
        world[SECOND_ROOM].name = m_cell_text;
        world[SECOND_ROOM].description = m_cell_text;
        m_cell_door = room_direction_data {};
        m_cell_door.keyword = m_cell_door_keyword;
        m_cell_door.general_description = m_cell_door_keyword;
        m_cell_door.key = NO_KEYHOLE;
        m_cell_door.to_room = 0;
        m_cell_door.exit_info = EX_ISDOOR;
        world[SECOND_ROOM].dir_option[NORTH] = &m_cell_door;

        weather_info.sunlight = SUN_RISE;
        return make_mob("huntress", 0, SECOND_ROOM);
    }

    // Whether <door> is closed and locked.
    static bool is_closed_and_locked(const room_direction_data* door)
    {
        return IS_SET(door->exit_info, EX_CLOSED) && IS_SET(door->exit_info, EX_LOCKED);
    }

    /* A writable empty argument for the commands. */
    static char* empty_argument()
    {
        static char empty[1] = "";
        empty[0] = '\0';
        return empty;
    }

    // Room 0's flags as SetUp found them; the tests make the room indoors.
    long m_saved_room_flags = 0;
    // The mob index the tests use: its first entry is index -1, a test mob's, and its second
    // entry 0, a risen ghoul's.
    index_data m_mob_index[2] {};
    // The mob table's one prototype, the risen ghoul's; affect_total reads its resistances.
    char_data m_risen_ghoul_prototype {};
    // The keywords and proficiencies a risen ghoul shares with its prototype.
    char m_risen_ghoul_keywords[8] = "risen";
    char_prof_data m_risen_ghoul_proficiencies {};
    // The mob table SetUp found, put back by TearDown.
    char_data* m_saved_mob_proto = nullptr;
    int m_saved_top_of_mobt = 0;
    // The mob index SetUp found, put back by TearDown.
    index_data* m_saved_mob_index = nullptr;
    // How many commands install_ring_then_stone_reset gives zone 0.
    static constexpr int RESET_COMMAND_COUNT = 3;
    // Zone 0's commands, written by install_ring_then_stone_reset.
    reset_com m_reset_commands[RESET_COMMAND_COUNT] {};
    // The sunlight SetUp found, put back by TearDown.
    int m_saved_sunlight = 0;
    // SECOND_ROOM's name and description as SetUp found them, put back by TearDown.
    char* m_saved_second_room_name = nullptr;
    char* m_saved_second_room_description = nullptr;
    // The cell's name and description, set by ready_huntress_at_sunrise.
    char m_cell_text[8] = "A cell";
    // The keyword and description of the cell's north door.
    char m_cell_door_keyword[9] = "irondoor";
    // The cell's north door, set by ready_huntress_at_sunrise.
    room_direction_data m_cell_door {};
};

} // namespace

/* A reset that puts a ring on its mob, then a stone in the last object loaded, run on a mob already
 * wearing a ring there: no second ring is made, and the stone has nothing to go in, and is not
 * made either. */
TEST_F(DeathAndDamagePathsTest, AResetMakesNoItemForAWearSlotItsMobHasFilled)
{
    char_data* keeper = make_mob("keeper", 0);
    obj_data* worn_ring = make_object(RING_VNUM, 0);
    ASSERT_NE(nullptr, worn_ring);
    equip_char(keeper, worn_ring, WEAR_FINGER_R);
    install_ring_then_stone_reset();

    reset_zone(0);

    EXPECT_EQ(worn_ring, keeper->equipment[WEAR_FINGER_R]);
    EXPECT_EQ(nullptr, worn_ring->contains);
    EXPECT_EQ(1, count_objects_of(RING_VNUM));
    EXPECT_EQ(0, count_objects_of(STONE_VNUM));
}

/* ---- an NPC olog-hai's skills ---- */

/* An NPC has no tactics; its olog-hai skills strike as on normal tactics. */
TEST_F(DeathAndDamagePathsTest, AnNpcOlogHaiStrikesAsOnNormalTactics)
{
    char_data* olog = make_mob("olog", 0);
    olog->player.race = RACE_OLOGHAI;
    constexpr int PROBABILITY = 50;
    /* get_base_skill_damage's formula: (BASE_DAMAGE + warrior level) * (PERCENT + probability),
     * divided by TACTICS_SCALE / tactics. */
    constexpr int BASE_DAMAGE = 2;
    constexpr int PERCENT = 100;
    constexpr int TACTICS_SCALE = 1000;
    const int expected = (BASE_DAMAGE + utils::get_prof_level(PROF_WARRIOR, *olog))
        * (PERCENT + PROBABILITY) / (TACTICS_SCALE / TACTICS_NORMAL);

    EXPECT_EQ(expected, olog_hai::get_base_skill_damage(*olog, PROBABILITY));
}

/* An NPC olog-hai's cleave hurts the mob next to it. */
TEST_F(DeathAndDamagePathsTest, AnNpcOlogHaiCleaveHurtsItsVictim)
{
    take_player_out_of_the_room();
    char_data* olog = make_mob("olog", 0);
    olog->player.race = RACE_OLOGHAI;
    constexpr int SURE_HIT_OB = 200; /* sure to land; a mob knows every skill at 80 */
    constexpr double LANDING_ROLL = 0.99; /* the cleave's to-hit roll: it lands */
    olog->points.OB = SURE_HIT_OB;
    equip_char(olog, make_object(SWORD_VNUM, 0), WIELD);
    char_data* victim = make_mob("victim", 0);
    push_test_random_value(LANDING_ROLL);

    do_cleave(olog, empty_argument(), nullptr, 0, 0);

    EXPECT_LT(GET_HIT(victim), MOB_HIT_POINTS);
}

/* ---- a summons ---- */

/* The one being summoned leaves the world while the summons is cast (a player who dies or rents
 * waits at the menu, still a character but in no room): the summons finds nobody, brings nobody
 * into the room, and tells the caster their victim has left the world. */
TEST_F(DeathAndDamagePathsTest, ASummonsOfSomeoneWhoLeftTheWorldBringsNobody)
{
    assign_command_pointers();
    link_second_room();
    char_from_room(&m_player);
    char_to_room(&m_player, SECOND_ROOM);
    /* In the game as a player is, so that the summons can find them. */
    register_pc_char(&m_player);
    m_player.next = character_list;
    character_list = &m_player;
    char_data* caster = make_mob("caster", 0);
    constexpr int CASTER_LEVEL = 50; /* a mob this strong never fumbles the spell */
    caster->player.level = CASTER_LEVEL;
    /* A connection for the caster, so that what it is told can be read. */
    descriptor_data caster_descriptor {};
    TestDescriptorRelease caster_descriptor_release(&caster_descriptor);
    caster_descriptor.output = caster_descriptor.small_outbuf;
    caster_descriptor.bufspace = SMALL_BUFSIZE - 1;
    caster_descriptor.connected = CON_PLYNG;
    caster_descriptor.character = caster;
    caster->desc = &caster_descriptor;
    char line[] = "cast 'summon' tester";
    command_interpreter(caster, line, nullptr);
    const bool cast_started = caster->delay.wait_value > 0;
    /* As extract_char leaves a player sent to the menu. */
    char_from_room(&m_player);
    for (char_data** link = &character_list; *link; link = &(*link)->next) {
        if (*link == &m_player) {
            *link = m_player.next;
            break;
        }
    }
    constexpr int ROLLS_QUEUED = 20; /* more than the summons draws */
    constexpr double LOWEST_ROLL = 0.0; /* the summoned one fails to resist */
    for (int roll = 0; roll < ROLLS_QUEUED; ++roll) {
        push_test_random_value(LOWEST_ROLL);
    }
    /* One summoned looks at the room they arrive in. */
    char room_text[] = "A test room";
    char* const saved_room_name = world[0].name;
    char* const saved_room_description = world[0].description;
    world[0].name = room_text;
    world[0].description = room_text;

    if (cast_started) {
        complete_delay(caster);
    }
    /* The caster leaves its connection behind before anything can fail and end the test. */
    caster->desc = nullptr;
    const std::string caster_output = caster_descriptor.output;

    world[0].name = saved_room_name;
    world[0].description = saved_room_description;
    remove_char_exists(m_player.abs_number);
    ASSERT_TRUE(cast_started) << "the cast did not start";
    EXPECT_EQ(NOWHERE, m_player.in_room);
    for (const char_data* person = world[0].people; person; person = person->next_in_room) {
        EXPECT_NE(&m_player, person) << "the player was summoned from the menu";
    }
    EXPECT_TRUE(is_in_game(caster));
    EXPECT_NE(caster_output.find("Your victim has left the world.\n\r"), std::string::npos)
        << caster_output;
}

/* ---- putting gear on and taking it off ---- */

/* A poisonous item hurts a healthy mob as it goes on, and stays worn. */
TEST_F(DeathAndDamagePathsTest, APoisonousItemHurtsItsNewWearerAndStaysOn)
{
    make_ring_and_sword_poisonous();
    char_data* wearer = make_mob("wearer", 0);
    obj_data* ring = make_object(RING_VNUM, 0);
    obj_to_char(ring, wearer);

    run_command_as(wearer, do_wear, "ring");

    ASSERT_TRUE(is_in_game(wearer));
    EXPECT_EQ(ring, wearer->equipment[WEAR_FINGER_R]);
    EXPECT_EQ(MOB_HIT_POINTS - POISON_DAMAGE, GET_HIT(wearer));
    EXPECT_TRUE(IS_AFFECTED(wearer, AFF_POISON));
}

/* The poison's blow sets off the wearer's damage script, which removes the wearer: the wearing
 * stops there. */
TEST_F(DeathAndDamagePathsTest, AWearerItsDamageScriptRemovesAsPoisonTakesHoldIsLetGo)
{
    make_ring_and_sword_poisonous();
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* wearer = make_mob("wearer", SCRIPT_VNUM);
    obj_to_char(make_object(RING_VNUM, 0), wearer);

    run_command_as(wearer, do_wear, "ring");

    EXPECT_FALSE(is_in_game(wearer));
}

/* A mob too hurt to survive the poison dies wielding a two-handed weapon: the wield does not go
 * on to take it in two hands. */
TEST_F(DeathAndDamagePathsTest, AMobThePoisonKillsAsItWieldsIsLetGo)
{
    make_ring_and_sword_poisonous();
    prototype_for(SWORD_VNUM).obj_flags.value[2] = TWO_HANDED_BULK;
    char_data* wielder = make_mob("wielder", 0);
    wielder->tmpabilities.hit = FEWER_HIT_POINTS_THAN_THE_POISON;
    obj_data* sword = make_object(SWORD_VNUM, 0);
    obj_to_char(sword, wielder);

    run_command_as(wielder, do_wield, "sword");

    EXPECT_FALSE(is_in_game(wielder));
    EXPECT_EQ(1, count_corpses_in_room());
    EXPECT_TRUE(is_in_object_list(sword));
}

/* Taking a poisonous item off hurts a healthy wearer, who keeps it. */
TEST_F(DeathAndDamagePathsTest, TakingAPoisonousItemOffHurtsTheWearerWhoKeepsIt)
{
    make_ring_and_sword_poisonous();
    char_data* wearer = make_mob("wearer", 0);
    obj_data* ring = make_object(RING_VNUM, 0);
    obj_to_char(ring, wearer);
    run_command_as(wearer, do_wear, "ring");
    ASSERT_EQ(ring, wearer->equipment[WEAR_FINGER_R]);

    run_command_as(wearer, do_remove, "ring");

    ASSERT_TRUE(is_in_game(wearer));
    EXPECT_EQ(MOB_HIT_POINTS - 2 * POISON_DAMAGE, GET_HIT(wearer));
    EXPECT_EQ(ring, wearer->carrying);
    EXPECT_EQ(nullptr, wearer->equipment[WEAR_FINGER_R]);
}

/* Taking the item off sets off the wearer's damage script, which removes the wearer: the item
 * lies on the floor where the wearer was. */
TEST_F(DeathAndDamagePathsTest, AWearerItsDamageScriptRemovesAsPoisonLeavesDropsTheItem)
{
    make_ring_and_sword_poisonous();
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* wearer = make_mob("wearer", 0);
    obj_data* ring = make_object(RING_VNUM, 0);
    obj_to_char(ring, wearer);
    run_command_as(wearer, do_wear, "ring");
    ASSERT_EQ(ring, wearer->equipment[WEAR_FINGER_R]);
    wearer->specials.script_number = SCRIPT_VNUM;

    run_command_as(wearer, do_remove, "all");

    EXPECT_FALSE(is_in_game(wearer));
    EXPECT_TRUE(is_on_the_floor(ring));
    EXPECT_EQ(nullptr, ring->carried_by);
}

/* A mob slain while it wears a poisonous item is stripped into one corpse: taking the item off
 * the dying mob does not kill it again. */
TEST_F(DeathAndDamagePathsTest, AMobSlainWearingAPoisonousItemLeavesOneCorpse)
{
    make_ring_and_sword_poisonous();
    char_data* wearer = make_mob("wearer", 0);
    obj_data* ring = make_object(RING_VNUM, 0);
    obj_to_char(ring, wearer);
    run_command_as(wearer, do_wear, "ring");
    ASSERT_EQ(ring, wearer->equipment[WEAR_FINGER_R]);
    wearer->tmpabilities.hit = FEWER_HIT_POINTS_THAN_THE_POISON;

    raw_kill(wearer, nullptr, 0);

    EXPECT_FALSE(is_in_game(wearer));
    ASSERT_EQ(1, count_corpses_in_room());
    EXPECT_TRUE(is_in_object_list(ring));
    EXPECT_FALSE(is_on_the_floor(ring));
}

/* A mob removed from the game while it wears a poisonous item is not hurt on its way out, so the
 * poison cannot kill it there; the item is left on the floor. */
TEST_F(DeathAndDamagePathsTest, AMobRemovedWearingAPoisonousItemIsRemovedOnce)
{
    make_ring_and_sword_poisonous();
    char_data* wearer = make_mob("wearer", 0);
    obj_data* ring = make_object(RING_VNUM, 0);
    obj_to_char(ring, wearer);
    run_command_as(wearer, do_wear, "ring");
    ASSERT_EQ(ring, wearer->equipment[WEAR_FINGER_R]);
    wearer->tmpabilities.hit = FEWER_HIT_POINTS_THAN_THE_POISON;

    extract_char(wearer);

    EXPECT_FALSE(is_in_game(wearer));
    EXPECT_EQ(0, count_corpses_in_room());
    EXPECT_TRUE(is_on_the_floor(ring));
}

/* ---- callers that take gear off or put it on ---- */

/* A mover its damage script removes as its stay-zone ring comes off leaves the ring behind, and
 * the rest of its gear is not looked at. */
TEST_F(DeathAndDamagePathsTest, AMoverRemovedAsItsStayZoneGearComesOffLeavesItBehind)
{
    make_ring_and_sword_poisonous();
    prototype_for(RING_VNUM).obj_flags.extra_flags |= ITEM_STAY_ZONE;
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* mover = make_mob_wearing_poisonous_ring("mover", SCRIPT_VNUM);
    obj_data* ring = mover->equipment[WEAR_FINGER_R];

    EXPECT_FALSE(prohibit_item_stay_zone_move(mover, 0));

    EXPECT_FALSE(is_in_game(mover));
    EXPECT_TRUE(is_on_the_floor(ring));
}

/* A mover the stay-zone ring only hurts as it comes off leaves it behind and goes on. */
TEST_F(DeathAndDamagePathsTest, AMoverHurtAsItsStayZoneGearComesOffLeavesItBehind)
{
    make_ring_and_sword_poisonous();
    prototype_for(RING_VNUM).obj_flags.extra_flags |= ITEM_STAY_ZONE;
    char_data* mover = make_mob_wearing_poisonous_ring("mover", 0);
    obj_data* ring = mover->equipment[WEAR_FINGER_R];

    EXPECT_TRUE(prohibit_item_stay_zone_move(mover, 0));

    ASSERT_TRUE(is_in_game(mover));
    EXPECT_TRUE(is_on_the_floor(ring));
    EXPECT_EQ(nullptr, mover->equipment[WEAR_FINGER_R]);
    EXPECT_EQ(MOB_HIT_POINTS - 2 * POISON_DAMAGE, GET_HIT(mover));
}

/* A zone reset puts a ring on a mob whose damage script, set off by the ring's poison, destroys
 * the ring: the stone meant for the last object loaded has nothing to go in, and is not made. */
TEST_F(DeathAndDamagePathsTest, AResetPutsNothingInAnItemItsWearerDestroyed)
{
    make_ring_and_sword_poisonous();
    install_script(ON_DAMAGE,
        { { SCRIPT_ASSIGN_EQ, nullptr, { SCRIPT_PARAM_CH1, SCRIPT_PARAM_OB1, WEAR_FINGER_R, SCRIPT_PARAM_INT1 } },
            { SCRIPT_EXTRACT_OBJ, nullptr, { SCRIPT_PARAM_OB1 } } });
    char_data* keeper = make_mob("keeper", SCRIPT_VNUM);
    install_ring_then_stone_reset();

    reset_zone(0);

    ASSERT_TRUE(is_in_game(keeper));
    EXPECT_EQ(nullptr, keeper->equipment[WEAR_FINGER_R]);
    EXPECT_EQ(0, count_objects_of(RING_VNUM));
    EXPECT_EQ(0, count_objects_of(STONE_VNUM));
}

/* The same reset on a mob the poison only hurts: the stone goes in the ring it wears. */
TEST_F(DeathAndDamagePathsTest, AResetPutsTheStoneInTheRingItsMobWears)
{
    make_ring_and_sword_poisonous();
    char_data* keeper = make_mob("keeper", 0);
    install_ring_then_stone_reset();

    reset_zone(0);

    obj_data* ring = keeper->equipment[WEAR_FINGER_R];
    ASSERT_NE(nullptr, ring);
    EXPECT_EQ(MOB_HIT_POINTS - POISON_DAMAGE, GET_HIT(keeper));
    ASSERT_NE(nullptr, ring->contains);
    EXPECT_EQ(real_number_of(STONE_VNUM), ring->contains->item_number);
}

/* Nobody in no room is hurt: a blow at someone being taken out of the game lands nowhere. */
TEST_F(DeathAndDamagePathsTest, ABlowAtSomeoneInNoRoomLandsNowhere)
{
    char_data* attacker = make_mob("attacker", 0);
    char_data* victim = make_mob("victim", 0);
    char_from_room(victim);

    EXPECT_EQ(0, damage(attacker, victim, MOB_HIT_POINTS * 2, TYPE_HIT, 0));

    EXPECT_TRUE(is_in_game(victim));
    EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(victim));
}

/* A mob killed from beyond the door it knocks on leaves its corpse, and what it carried, on its
 * own side. */
TEST_F(DeathAndDamagePathsTest, AKnockerKilledFromBeyondTheDoorFallsOnItsOwnSide)
{
    link_second_room();
    char door_keyword[] = "door";
    room_direction_data* door = world[0].dir_option[NORTH];
    door->exit_info = EX_ISDOOR;
    door->keyword = door_keyword;
    char_data* doorkeeper = make_mob("doorkeeper", 0, SECOND_ROOM);
    make_into_risen_ghoul(doorkeeper);
    SET_BIT(MOB_FLAGS(doorkeeper), MOB_SPEC);
    mob_index[doorkeeper->nr].func = kill_whoever_knocks;
    char_data* knocker = make_mob("knocker", 0);
    obj_to_char(make_object(STONE_VNUM, 0), knocker);

    run_command_as(knocker, do_knock, "door");

    EXPECT_FALSE(is_in_game(knocker));
    EXPECT_TRUE(is_in_game(doorkeeper));
    EXPECT_EQ(1, count_corpses_in_room());
    EXPECT_EQ(nullptr, world[SECOND_ROOM].contents);
}

// ---- dying of negative regeneration ----

// A mob whose regeneration drains its last hit points dies of it, and its death is noted
// without touching what is left of it.
TEST_F(DeathAndDamagePathsTest, AMobNegativeRegenerationKillsIsLetGo)
{
    char_data* drained = make_mob("drained", 0);
    drained->tmpabilities.hit = 0;
    drained->points.health_regen = DRAINING_REGENERATION;

    fast_update();

    EXPECT_FALSE(is_in_game(drained));
    EXPECT_EQ(1, count_corpses_in_room());
}

// ---- spells ----

// The shout's blow at the first mob sets off a script that removes the mob after it: the shout
// strikes the first and stops there.
TEST_F(DeathAndDamagePathsTest, AShoutOfPainStopsAtTheMobItsBlowRemoves)
{
    take_player_out_of_the_room();
    install_hurt_then_vanish_scripts();
    char_data* caster = make_mob("caster", 0);
    char_data* first = make_mob("first", SCRIPT_VNUM);
    char_data* second = make_mob("second", OTHER_SCRIPT_VNUM);
    ASSERT_EQ(second, first->next_in_room);

    spell_shout_of_pain(caster, empty_argument(), SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    EXPECT_TRUE(is_in_game(caster));
    EXPECT_TRUE(is_in_game(first));
    EXPECT_LT(GET_HIT(first), MOB_HIT_POINTS);
    EXPECT_FALSE(is_in_game(second));
}

// The shout's blow at the first mob sets off its damage script, which removes the caster: the
// shout ends with the caster, and the mob after it is not struck.
TEST_F(DeathAndDamagePathsTest, AShoutOfPainEndsWithTheCasterItsBlowRemoves)
{
    take_player_out_of_the_room();
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    char_data* caster = make_mob("caster", 0);
    char_data* first = make_mob("first", SCRIPT_VNUM);
    char_data* second = make_mob("second", 0);

    spell_shout_of_pain(caster, empty_argument(), SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    EXPECT_FALSE(is_in_game(caster));
    EXPECT_TRUE(is_in_game(first));
    ASSERT_TRUE(is_in_game(second));
    EXPECT_EQ(MOB_HIT_POINTS, GET_HIT(second));
}

// As for the shout: an earthquake stops at the mob its blow removes.
TEST_F(DeathAndDamagePathsTest, AnEarthquakeStopsAtTheMobItsBlowRemoves)
{
    take_player_out_of_the_room();
    install_hurt_then_vanish_scripts();
    char_data* caster = make_mob("caster", 0);
    char_data* first = make_mob("first", SCRIPT_VNUM);
    char_data* second = make_mob("second", OTHER_SCRIPT_VNUM);
    ASSERT_EQ(second, first->next_in_room);

    spell_earthquake(caster, empty_argument(), SPELL_TYPE_SPELL, nullptr, nullptr, 0, 0);

    EXPECT_TRUE(is_in_game(caster));
    EXPECT_LT(GET_HIT(first), MOB_HIT_POINTS);
    EXPECT_FALSE(is_in_game(second));
}

// A blaze burning in the room strikes the first mob, whose script removes the mob after it: the
// blaze burns nobody more this time.
TEST_F(DeathAndDamagePathsTest, ARoomBlazeStopsAtTheMobItsBlowRemoves)
{
    take_player_out_of_the_room();
    install_hurt_then_vanish_scripts();
    char_data* first = make_mob("first", SCRIPT_VNUM);
    char_data* second = make_mob("second", OTHER_SCRIPT_VNUM);
    ASSERT_EQ(first, world[0].people);
    ASSERT_EQ(second, first->next_in_room);
    affected_type blaze {};
    blaze.type = ROOMAFF_SPELL;
    blaze.duration = ROOM_SPELL_HOURS;
    blaze.modifier = BLAZE_STRENGTH;
    blaze.location = SPELL_BLAZE;
    affect_to_room(&world[0], &blaze);
    push_test_random_value(MIST_STAYS_ROLL); // whether a mist would move: there is none
    push_test_random_value(ROOM_SPELL_STRIKES_ROLL); // the blaze strikes the first

    affect_update_room(&world[0]);

    EXPECT_LT(GET_HIT(first), MOB_HIT_POINTS);
    EXPECT_FALSE(is_in_game(second));
    while (world[0].affected) {
        affect_remove_room(&world[0], world[0].affected);
    }
}

// A mob finishes casting a shout of pain whose blow sets off a script that removes it: the cast
// ends there, leaving the delay that went with the caster alone.
TEST_F(DeathAndDamagePathsTest, ACastEndsWithTheCasterItsSpellRemoves)
{
    assign_command_pointers();
    take_player_out_of_the_room();
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    char_data* caster = make_mob("caster", 0);
    caster->player.level = UNFUMBLING_CASTER_LEVEL;
    char_data* victim = make_mob("victim", SCRIPT_VNUM);
    char line[] = "cast 'shout of pain'";
    command_interpreter(caster, line, nullptr);
    ASSERT_GT(caster->delay.wait_value, 0) << "the cast did not start";

    complete_delay(caster);

    EXPECT_FALSE(is_in_game(caster));
    EXPECT_TRUE(is_in_game(victim));
}

/* ---- a walker removed at the end of its route ---- */

namespace {

/* Takes <walker> to the end of a path route that is room 0 alone and removes the walker there
 * (path_complete_extract): its last wander roll queues it for walker_remove_finished. Mob table
 * entry 0 carries the route only for that roll, and the walker is that entry's mob only then. */
void finish_walker_route(char_data* walker)
{
    std::string options = "walker_type=path\n\rwalker_rooms=" + std::to_string(ROOM_VNUM)
        + "\n\rpath_complete_extract=yes\n\r";
    mob_proto[0].specials.mob_options = options.data();
    walker_config_rebuild(0);
    const int walker_number = walker->nr;
    walker->nr = 0;
    const walker_result result = walker_wander(walker, NORTH, true);
    walker->nr = walker_number;
    mob_proto[0].specials.mob_options = nullptr;
    walker_config_rebuild(0);
    EXPECT_EQ(WALKER_HANDLED, result);
}

} // namespace

/* A walker removed at the end of its route takes its gear with it, and nothing is left behind. */
TEST_F(DeathAndDamagePathsTest, AWalkerAtTheEndOfItsRouteLeavesWithItsGear)
{
    link_second_room();
    make_ring_and_sword_poisonous();
    char_data* walker = make_mob_wearing_poisonous_ring("walker", 0);
    finish_walker_route(walker);

    walker_remove_finished();

    EXPECT_FALSE(is_in_game(walker));
    EXPECT_EQ(0, count_corpses_in_room());
    EXPECT_EQ(0, count_objects_of(RING_VNUM));
}

/* The poison of the ring coming off a walker removed at the end of its route kills it: it dies
 * there, leaving its corpse, and is not removed a second time. */
TEST_F(DeathAndDamagePathsTest, AWalkerItsGearKillsAsItLeavesAtTheEndOfItsRouteIsLetGo)
{
    link_second_room();
    make_ring_and_sword_poisonous();
    char_data* walker = make_mob_wearing_poisonous_ring("walker", 0);
    walker->tmpabilities.hit = FEWER_HIT_POINTS_THAN_THE_POISON;
    finish_walker_route(walker);

    walker_remove_finished();

    EXPECT_FALSE(is_in_game(walker));
    EXPECT_EQ(1, count_corpses_in_room());
    EXPECT_EQ(0, count_objects_of(RING_VNUM));
}

// ---- a ghoul sinking back into the ground ----

// A dismissed ghoul that the poison of the gear it loses kills dies there: it is not removed a
// second time, and its body is left.
TEST_F(DeathAndDamagePathsTest, ADismissedGhoulThePoisonOfItsGearKillsIsLetGo)
{
    make_ring_and_sword_poisonous();
    char_data* summoner = make_mob("ghoul", 0);
    char_data* risen = make_mob("risen", 0);
    make_into_risen_ghoul(risen);
    add_follower(risen, summoner, FOLLOW_MOVE);
    obj_to_char(make_object(RING_VNUM, 0), risen);
    obj_to_char(make_object(SWORD_VNUM, 0), risen);
    run_command_as(risen, do_wear, "ring");
    run_command_as(risen, do_wield, "sword");
    ASSERT_NE(nullptr, risen->equipment[WEAR_FINGER_R]);
    ASSERT_NE(nullptr, risen->equipment[WIELD]);
    risen->tmpabilities.hit = FEWER_HIT_POINTS_THAN_THE_POISON;
    push_test_random_value(LOWEST_ROLL); // the idle follower is dismissed
    char no_argument[1] = "";

    ghoul(summoner, summoner, 0, no_argument, SPECIAL_SELF, nullptr);

    EXPECT_FALSE(is_in_game(risen));
    EXPECT_TRUE(is_in_game(summoner));
    EXPECT_EQ(nullptr, summoner->followers);
    EXPECT_EQ(1, count_corpses_in_room());
}

// A ghoul fading away is removed by its damage script as its ring comes off: the fading stops
// there. The ring goes as the rest of its gear does.
TEST_F(DeathAndDamagePathsTest, AFadingGhoulItsGearRemovesLeavesTheRing)
{
    make_ring_and_sword_poisonous();
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* summoner = make_mob("ghoul", 0);
    char_data* risen = make_mob_wearing_poisonous_ring("risen", SCRIPT_VNUM);
    make_into_risen_ghoul(risen);
    obj_data* ring = risen->equipment[WEAR_FINGER_R];
    add_follower(risen, summoner, FOLLOW_MOVE);
    push_test_random_value(LOWEST_ROLL); // the idle follower is dismissed
    char no_argument[1] = "";

    ghoul(summoner, summoner, 0, no_argument, SPECIAL_SELF, nullptr);

    EXPECT_FALSE(is_in_game(risen));
    EXPECT_TRUE(is_in_game(summoner));
    EXPECT_FALSE(is_in_object_list(ring));
}

// A dismissed ghoul that survives losing its gear sinks into the ground as before: the gear is
// destroyed and nothing is left.
TEST_F(DeathAndDamagePathsTest, ADismissedGhoulThatSurvivesItsGearSinksIntoTheGround)
{
    char_data* summoner = make_mob("ghoul", 0);
    char_data* risen = make_mob("risen", 0);
    make_into_risen_ghoul(risen);
    add_follower(risen, summoner, FOLLOW_MOVE);
    obj_data* ring = make_object(RING_VNUM, 0);
    obj_to_char(ring, risen);
    run_command_as(risen, do_wear, "ring");
    ASSERT_EQ(ring, risen->equipment[WEAR_FINGER_R]);
    push_test_random_value(LOWEST_ROLL); // the idle follower is dismissed
    char no_argument[1] = "";

    ghoul(summoner, summoner, 0, no_argument, SPECIAL_SELF, nullptr);

    EXPECT_FALSE(is_in_game(risen));
    EXPECT_FALSE(is_in_object_list(ring));
    EXPECT_EQ(0, count_corpses_in_room());
}

// A player following an idle ghoul is not dismissed with its risen ghouls: the player keeps
// what they wear and stays, and the risen ghoul behind them sinks into the ground.
TEST_F(DeathAndDamagePathsTest, APlayerFollowingAGhoulKeepsTheirGearAndStays)
{
    char_data* summoner = make_mob("ghoul", 0);
    char_data* risen = make_mob("risen", 0);
    make_into_risen_ghoul(risen);
    add_follower(risen, summoner, FOLLOW_MOVE);
    // In the game as a player is; followed last, so first among the ghoul's followers.
    register_pc_char(&m_player);
    m_player.next = character_list;
    character_list = &m_player;
    add_follower(&m_player, summoner, FOLLOW_MOVE);
    obj_data* ring = make_object(RING_VNUM, 0);
    obj_to_char(ring, &m_player);
    run_command(do_wear, "ring");
    ASSERT_EQ(ring, m_player.equipment[WEAR_FINGER_R]);
    ASSERT_EQ(&m_player, summoner->followers->follower);
    push_test_random_value(LOWEST_ROLL); // an idle follower is dismissed
    char no_argument[1] = "";

    ghoul(summoner, summoner, 0, no_argument, SPECIAL_SELF, nullptr);

    const bool player_in_game = is_in_game(&m_player);
    const bool player_in_room = m_player.in_room == 0;
    obj_data* const worn_ring = m_player.equipment[WEAR_FINGER_R];
    stop_follower(&m_player, FOLLOW_MOVE);
    for (char_data** link = &character_list; *link; link = &(*link)->next) {
        if (*link == &m_player) {
            *link = m_player.next;
            break;
        }
    }
    remove_char_exists(m_player.abs_number);
    EXPECT_TRUE(player_in_game);
    EXPECT_TRUE(player_in_room);
    EXPECT_EQ(ring, worn_ring);
    EXPECT_TRUE(is_in_object_list(ring));
    EXPECT_FALSE(is_in_game(risen));
}

// ---- Thuringwethil flying back to her tower ----

// The Vampire Huntress, badly hurt in a fight, flies to her tower and destroys her gear there: the
// poison of her ring kills her as it comes off, and she is let go, leaving her body in the tower
// and no human form behind.
TEST_F(DeathAndDamagePathsTest, AFleeingVampireHuntressThePoisonOfHerGearKillsIsLetGo)
{
    link_second_room();
    world[SECOND_ROOM].number = VAMPIRE_TOWER_VNUM;
    make_ring_and_sword_poisonous();
    char_data* huntress = make_mob_wearing_poisonous_ring("huntress", 0);
    obj_data* ring = huntress->equipment[WEAR_FINGER_R];
    huntress->specials.position = POSITION_FIGHTING;
    huntress->tmpabilities.hit = FEWER_HIT_POINTS_THAN_THE_POISON;
    char no_argument[1] = "";

    EXPECT_EQ(1, vampire_huntress(huntress, huntress, 0, no_argument, SPECIAL_SELF, nullptr));

    EXPECT_FALSE(is_in_game(huntress));
    EXPECT_FALSE(is_in_object_list(ring));
    EXPECT_EQ(nullptr, world[SECOND_ROOM].people);
    const obj_data* body = world[SECOND_ROOM].contents;
    ASSERT_NE(nullptr, body);
    EXPECT_EQ(ITEM_CONTAINER, GET_ITEM_TYPE(body));
    EXPECT_EQ(nullptr, body->next_content);
}

// Thuringwethil, fully healed, melts away to her tower and destroys her gear there: the poison
// of her ring sets off her damage script, which removes her, and she is let go with no bat form
// behind. Being at full health, she always outlives the poison itself.
TEST_F(DeathAndDamagePathsTest, AHealedThuringwethilHerGearRemovesIsLetGo)
{
    link_second_room();
    world[SECOND_ROOM].number = VAMPIRE_TOWER_VNUM;
    make_ring_and_sword_poisonous();
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* vampire = make_mob_wearing_poisonous_ring("thuringwethil", SCRIPT_VNUM);
    obj_data* ring = vampire->equipment[WEAR_FINGER_R];
    vampire->abilities.hit = 2 * THURINGWETHIL_BEATEN_HIT_POINTS;
    vampire->tmpabilities.hit = 2 * THURINGWETHIL_BEATEN_HIT_POINTS;
    char no_argument[1] = "";

    EXPECT_EQ(1, thuringwethil(vampire, vampire, 0, no_argument, SPECIAL_SELF, nullptr));

    EXPECT_FALSE(is_in_game(vampire));
    EXPECT_FALSE(is_in_object_list(ring));
    EXPECT_EQ(nullptr, world[SECOND_ROOM].people);
    EXPECT_EQ(nullptr, world[SECOND_ROOM].contents);
}

// The Vampire Huntress, badly hurt, becomes a woman in her tower, and her human form dies of the
// poison of the first ring she puts on there: the form is let go, and walks nowhere.
TEST_F(DeathAndDamagePathsTest, AVampireHuntressHumanFormThePoisonOfHerNewGearKillsIsLetGo)
{
    link_second_room();
    world[SECOND_ROOM].number = VAMPIRE_TOWER_VNUM;
    make_mob_table_her_human_form();
    make_her_first_item_a_poisonous_ring();
    char_data* huntress = make_mob("huntress", 0);
    huntress->specials.position = POSITION_FIGHTING;
    huntress->tmpabilities.hit = HUNTRESS_HIT_POINTS_HER_HUMAN_FORM_CANNOT_SURVIVE;
    char no_argument[1] = "";

    EXPECT_EQ(1, vampire_huntress(huntress, huntress, 0, no_argument, SPECIAL_SELF, nullptr));

    EXPECT_FALSE(is_in_game(huntress));
    EXPECT_EQ(nullptr, world[SECOND_ROOM].people);
    const obj_data* body = world[SECOND_ROOM].contents;
    ASSERT_NE(nullptr, body);
    EXPECT_TRUE(is_corpse(body));
    EXPECT_EQ(nullptr, body->next_content);
}

// ---- the Vampire Huntress carrying a prisoner off ----

// She carries the player off to their cell and slams the cell's door shut from both sides.
TEST_F(DeathAndDamagePathsTest, AVampireHuntressLocksHerPrisonersCellFromBothSides)
{
    char_data* huntress = ready_huntress_at_sunrise();
    push_test_random_value(ABDUCTION_ROLL);
    char no_argument[1] = "";

    vampire_huntress(huntress, huntress, 0, no_argument, SPECIAL_SELF, nullptr);

    EXPECT_EQ(SECOND_ROOM, m_player.in_room);
    EXPECT_EQ(0, huntress->in_room);
    EXPECT_TRUE(is_closed_and_locked(world[SECOND_ROOM].dir_option[NORTH]));
    EXPECT_TRUE(is_closed_and_locked(world[0].dir_option[SOUTH]));
    EXPECT_TRUE(output_has("The iron door slams shut.")) << output();
}

// The cell has no north door: that is logged, and the prisoner is left in the cell as before.
TEST_F(DeathAndDamagePathsTest, AVampireHuntressPrisonersCellWithNoDoorIsLogged)
{
    char_data* huntress = ready_huntress_at_sunrise();
    world[SECOND_ROOM].dir_option[NORTH] = nullptr;
    push_test_random_value(ABDUCTION_ROLL);
    char no_argument[1] = "";

    testing::internal::CaptureStderr();
    vampire_huntress(huntress, huntress, 0, no_argument, SPECIAL_SELF, nullptr);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(SECOND_ROOM, m_player.in_room);
    EXPECT_EQ(0, huntress->in_room);
    EXPECT_NE(std::string::npos, logged.find("vampire_huntress: room 15398 has no north exit"))
        << logged;
    EXPECT_FALSE(output_has("The iron door slams shut.")) << output();
}

// The cell's door has no far side in the tower: that is logged, and the cell's side is still
// closed and locked.
TEST_F(DeathAndDamagePathsTest, AVampireHuntressCellDoorWithNoFarSideIsLogged)
{
    char_data* huntress = ready_huntress_at_sunrise();
    world[0].dir_option[SOUTH] = nullptr;
    push_test_random_value(ABDUCTION_ROLL);
    char no_argument[1] = "";

    testing::internal::CaptureStderr();
    vampire_huntress(huntress, huntress, 0, no_argument, SPECIAL_SELF, nullptr);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_EQ(SECOND_ROOM, m_player.in_room);
    EXPECT_TRUE(is_closed_and_locked(world[SECOND_ROOM].dir_option[NORTH]));
    EXPECT_NE(std::string::npos, logged.find("vampire_huntress: room 15379 has no south exit"))
        << logged;
}

// The Huntress's script paused when she heard the player; carrying the player off starts her own
// delay, which ends the pause, and the rest of her script removes her: she is let go there.
TEST_F(DeathAndDamagePathsTest, AVampireHuntressHerResumedScriptRemovesAsSheCarriesThePlayerOffIsLetGo)
{
    install_script(ON_HEAR_SAY,
        { { SCRIPT_DO_WAIT, nullptr, { SCRIPT_PAUSE_PULSES } },
            { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } } });
    char_data* huntress = ready_huntress_at_sunrise();
    char_from_room(huntress);
    char_to_room(huntress, 0);
    huntress->specials.script_number = SCRIPT_VNUM;
    run_command(do_say, "hello");
    ASSERT_EQ(CMD_SCRIPT, huntress->delay.cmd);
    push_test_random_value(ABDUCTION_ROLL);
    char no_argument[1] = "";

    EXPECT_EQ(1, vampire_huntress(huntress, huntress, 0, no_argument, SPECIAL_SELF, nullptr));

    EXPECT_FALSE(is_in_game(huntress));
    EXPECT_EQ(SECOND_ROOM, m_player.in_room);
}

// The Huntress attacks the player with a weapon whose damage script removes its wielder: she is
// let go there.
TEST_F(DeathAndDamagePathsTest, AVampireHuntressHerWeaponsDamageScriptRemovesAsSheAttacksIsLetGo)
{
    install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } });
    char_data* huntress = ready_huntress_at_sunrise();
    ASSERT_TRUE(equip_char(huntress, make_object(SWORD_VNUM, SCRIPT_VNUM), WIELD));
    GET_ENERGY(huntress) = ENE_TO_HIT;
    push_test_random_value(ATTACK_ROLL);
    push_test_random_value(SURE_HIT_ROLL);
    char no_argument[1] = "";

    EXPECT_EQ(1, vampire_huntress(huntress, huntress, 0, no_argument, SPECIAL_SELF, nullptr));

    EXPECT_FALSE(is_in_game(huntress));
    EXPECT_EQ(0, m_player.in_room);
}
