/* Putting gear on and taking it off, dying of negative regeneration, area spells and an NPC
 * olog-hai's skills, when a blow, a death or a script set off on the way removes one of the
 * characters involved: nothing touches the removed one, and what stays behaves as it does
 * without the removal.
 *
 * Poison here comes from an item that gives its wearer the poison flag (APPLY_BITVECTOR). */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../char_utils.h"
#include "../comm.h" /* complete_delay */
#include "../skill_timer.h"
#include "../spells.h"

ACMD(do_wear);
ACMD(do_wield);
ACMD(do_remove);
ACMD(do_cleave);
ACMD(do_knock);
ACMD(do_order);
void assign_command_pointers(void);
void raw_kill(char_data* dead_man, const char_data* killer, int attack_type);
void fast_update();
SPECIAL(ghoul);
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
    }

    void TearDown() override
    {
        world[0].room_flags = m_saved_room_flags;
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
};

} // namespace
