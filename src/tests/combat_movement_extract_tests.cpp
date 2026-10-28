/* Moving, fleeing and fighting while a script, set off on the way, removes one of the characters
 * involved (EXTRACT_CHAR): whoever was moving, fleeing or fighting stops touching the removed one.
 *
 * The scripts here belong to mobs waiting in the second room or to the one being hit. A test
 * that needs a roll to go one way queues it with push_test_random_value. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../skill_timer.h"
#include "../spells.h"

void do_move(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_flee(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_kick(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_say(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void hit(char_data* ch, char_data* victim, int type);
void perform_violence();
void set_fighting(char_data* ch, char_data* victim);
void on_windblast_success(char_data* ch, int mana_cost, int move_cost);
void die(char_data* dead_man, char_data* killer, int attack_type);
void do_rend(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_bite(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_maul(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_cleave(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_stomp(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_smash(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_bash(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_rescue(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_disengage(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_track(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void affect_update_person(struct char_data* i, int mode);
void assign_command_pointers(void);
SPECIAL(thuringwethil);

extern struct index_data* mob_index;
extern int top_of_mobt;
extern struct weather_data weather_info;
extern struct txt_block* txt_block_pool;

using namespace scripted_room;

namespace {

/* Enough movement points for any step. */
constexpr int PLENTY_OF_MOVES = 100;

class CombatMovementTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        link_second_room();
        /* Made at boot; the skills ask them. Making them again keeps the ones there are. */
        game_rules::big_brother::create(weather_info, &world);
        game_timer::skill_timer::create(weather_info, &world);
    }

    void TearDown() override
    {
        ScriptedRoomFixture::TearDown();
        if (m_saved_mob_index != nullptr) {
            mob_index = m_saved_mob_index;
        }
        RELEASE(m_dying_profs);
    }

    /* Gives <dying> the one prototype entry, which make_corpse reads. With a prototype the game
     * no longer frees the mob's strings or profession block, so the fixture owns them. */
    void give_prototype(char_data* dying)
    {
        m_saved_mob_index = mob_index;
        mob_index = m_prototype_index;
        RELEASE(dying->player.name);
        RELEASE(dying->player.short_descr);
        dying->player.name = m_dying_name;
        dying->player.short_descr = m_dying_name;
        m_dying_profs = dying->profs;
        dying->nr = 0;
    }

    /* Makes script SCRIPT_VNUM say "avenge me" as its mob dies, and script OTHER_SCRIPT_VNUM
     * remove its own mob when it hears anything. */
    void install_avenge_me_scripts()
    {
        const body_line last_words = { SCRIPT_DO_SAY, "avenge me", { SCRIPT_PARAM_CH1 } };
        const body_line vanish = { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } };
        install_scripts({ { SCRIPT_VNUM, { { ON_DIE, { last_words } } } },
            { OTHER_SCRIPT_VNUM, { { ON_HEAR_SAY, { vanish } } } } });
    }

    /* Gives <owner> a bag holding a ring, and returns the ring. */
    obj_data* give_ring_in_bag(char_data* owner)
    {
        obj_data* bag = make_object(BAG_VNUM, 0);
        obj_data* ring = make_object(RING_VNUM, 0);
        obj_to_obj(ring, bag);
        obj_to_char(bag, owner);
        return ring;
    }

    // The mob table give_prototype points the game at; its one entry is the dying mob's.
    index_data m_prototype_index[1] {};
    // The game's mob table while give_prototype has replaced it; null when it has not.
    index_data* m_saved_mob_index = nullptr;
    // The dying mob's name and short description once give_prototype has made them the test's.
    char m_dying_name[6] = "dying";
    // The dying mob's profession block; TearDown frees it, as its prototype keeps the game off it.
    char_prof_data* m_dying_profs = nullptr;

    /* Makes script SCRIPT_VNUM a guard that removes whoever triggers <trigger_type> in its room
     * (ch2), and script OTHER_SCRIPT_VNUM a watcher that tells the room it saw someone. */
    void install_guard_and_watcher(int trigger_type)
    {
        install_scripts({ { SCRIPT_VNUM,
                              { { trigger_type, { { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH2 } } } } } },
            { OTHER_SCRIPT_VNUM,
                { { trigger_type,
                    { { SCRIPT_SEND_TO_ROOM, "The watcher looks up.", { SCRIPT_PARAM_CH1_ROOM } } } } } } });
    }

    /* Makes script SCRIPT_VNUM pause on hearing anything, then say "vanish", and script
     * OTHER_SCRIPT_VNUM remove its own mob when it hears that word. The rest of a paused script
     * names nobody but its mob, so it removes someone else through what it sets off, as here. */
    void install_pause_then_vanish_scripts()
    {
        install_scripts({ { SCRIPT_VNUM,
                              { { ON_HEAR_SAY,
                                  { { SCRIPT_DO_WAIT, nullptr, { 5 } },
                                      { SCRIPT_DO_SAY, "vanish", { SCRIPT_PARAM_CH1 } } } } } },
            { OTHER_SCRIPT_VNUM,
                { { ON_HEAR_SAY,
                    { { SCRIPT_IF_STR_CONTAINS, "VANISH", { SCRIPT_PARAM_STR1 } },
                        { SCRIPT_BEGIN, nullptr, {} },
                        { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } },
                        { SCRIPT_END, nullptr, {} } } } } } });
    }

    /* A mob in room 0 with the moves to walk anywhere. */
    char_data* make_walker(const char* keywords)
    {
        char_data* walker = make_mob(keywords, 0);
        walker->abilities.move = PLENTY_OF_MOVES;
        walker->tmpabilities.move = PLENTY_OF_MOVES;
        return walker;
    }

    /* Seats <rider> on <mount>, behind any rider already there, as do_ride leaves them. */
    static void seat(char_data* rider, char_data* mount)
    {
        rider->mount_data.mount = mount;
        rider->mount_data.mount_number = mount->abs_number;
        rider->mount_data.next_rider = nullptr;
        rider->mount_data.next_rider_number = -1;
        if (!mount->mount_data.rider) {
            mount->mount_data.rider = rider;
            mount->mount_data.rider_number = rider->abs_number;
            return;
        }
        char_data* last = mount->mount_data.rider;
        while (last->mount_data.next_rider) {
            last = last->mount_data.next_rider;
        }
        last->mount_data.next_rider = rider;
        last->mount_data.next_rider_number = rider->abs_number;
    }

    /* Makes <victim> a wimpy mob badly hurt enough to flee from its next blow. */
    static void make_ready_to_flee(char_data* victim)
    {
        SET_BIT(MOB_FLAGS(victim), MOB_WIMPY);
        victim->abilities.move = PLENTY_OF_MOVES;
        victim->tmpabilities.move = PLENTY_OF_MOVES;
        victim->tmpabilities.hit = 1;
        victim->specials.position = POSITION_FIGHTING;
    }

    /* Moves <walker> north, as the interpreter does for the north command. */
    static void walk_north(char_data* walker) { do_move(walker, empty_argument(), nullptr, NORTH + 1, 0); }

    /* A writable empty argument for the commands. */
    static char* empty_argument()
    {
        static char empty[1] = "";
        empty[0] = '\0';
        return empty;
    }
};

} // namespace
