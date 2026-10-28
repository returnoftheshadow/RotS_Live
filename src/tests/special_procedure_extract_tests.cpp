/* Special procedures that remove characters or objects while a command, a move, a mob's turn or a
 * dragon's breath is still going through them: whatever offered the procedure stops touching
 * what was removed, and goes on as before with what is left.
 *
 * The procedures here are test procedures given to mobs (through a prototype entry), objects
 * (through their prototype's index entry) and rooms; procedure_record tells what they did. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../skill_timer.h"
#include "../spells.h"

#include <algorithm>
#include <deque>
#include <string>
#include <vector>

void do_move(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_say(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_flee(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_knock(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_gen_com(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_at(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_order(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_mental(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
void do_concentrate(char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd);
combat_result_struct damage_stat(char_data* killer, char_data* victim, int stat_num, int amount);
ASPELL(spell_curse);
void raw_kill(char_data* dead_man, const char_data* killer, int attack_type);
void one_mobile_activity(char_data* ch);
void mobile_activity(void);
void assign_command_pointers(void);
SPECIAL(dragon);
SPECIAL(gatekeeper);
SPECIAL(gatekeeper2);
SPECIAL(thuringwethil);
SPECIAL(guild);
SPECIAL(ar_tarthalon);
SPECIAL(mob_ranger);

extern struct index_data* mob_index;
extern int top_of_mobt;
extern struct weather_data weather_info;

using namespace scripted_room;

namespace {

/* Enough movement points for any step. */
constexpr int PLENTY_OF_MOVES = 100;
/* The interpreter's number for flee; do_flee offers this plus one to the procedures. */
constexpr int FLEE_COMMAND = 84;
/* Hit points at which Thuringwethil leaves whenever her procedure runs. */
constexpr int THURINGWETHIL_BEATEN_HIT_POINTS = 400;
/* The lowest stat a mental attack does not count as critical; one point less makes a wimpy mob
 * flee. */
constexpr int LOWEST_SAFE_STAT = 3;
/* A stat no test's mental attack brings near zero. */
constexpr int SAFE_STAT = 50;
/* How many mobs one test can give a prototype entry (and with it a procedure). */
constexpr int MOB_PROTOTYPE_SLOTS = 8;

/* What the test procedures act on and what they saw, cleared before each test. */
struct procedure_record {
    // The character remove_marked_character and remove_marked_on_death remove.
    char_data* character_to_remove = nullptr;
    // The object remove_marked_object removes.
    obj_data* object_to_remove = nullptr;
    // The hosts record_offer ran for, in order.
    std::vector<const void*> offered_hosts;
};

procedure_record record;

/* Notes that <host> was offered the command. It reads the one acting, as most procedures do, so
 * a freed one handed to it is a use after free that AddressSanitizer reports. */
SPECIAL(record_offer)
{
    if (ch && ch->in_room == NOWHERE) {
        ADD_FAILURE() << "the one acting is nowhere";
    }
    record.offered_hosts.push_back(host);
    return 0;
}

/* Takes the command for itself: no procedure after it is offered it. */
SPECIAL(intercept_command)
{
    record.offered_hosts.push_back(host);
    return 1;
}

/* Removes its own mob, and lets the command go on. */
SPECIAL(remove_own_mob)
{
    extract_char(host);
    return 0;
}

/* Removes its own object, and lets the command go on. */
SPECIAL(remove_own_object)
{
    extract_obj(reinterpret_cast<obj_data*>(host));
    return 0;
}

/* Removes the one acting, and lets the command go on. */
SPECIAL(remove_actor)
{
    extract_char(ch);
    return 0;
}

/* A guard: removes whoever sets off north past it, and lets the command go on. */
SPECIAL(remove_actor_going_north)
{
    if (callflag == SPECIAL_COMMAND && cmd == NORTH + 1) {
        extract_char(ch);
    }
    return 0;
}

/* Removes record.character_to_remove when anyone acts, and lets the command go on. */
SPECIAL(remove_marked_character)
{
    if (record.character_to_remove) {
        char_data* marked = record.character_to_remove;
        record.character_to_remove = nullptr;
        extract_char(marked);
    }
    return 0;
}

/* As remove_marked_character, but only when someone sets off north. */
SPECIAL(remove_marked_going_north)
{
    if (callflag == SPECIAL_COMMAND && cmd == NORTH + 1) {
        return remove_marked_character(host, ch, cmd, arg, callflag, wtl);
    }
    return 0;
}

/* As remove_marked_character, but only for the one arriving being record.character_to_remove. */
SPECIAL(remove_marked_arriving)
{
    if (callflag == SPECIAL_ENTER && ch == record.character_to_remove) {
        return remove_marked_character(host, ch, cmd, arg, callflag, wtl);
    }
    return 0;
}

/* As remove_marked_character, but only when its own mob dies. */
SPECIAL(remove_marked_on_death)
{
    if (callflag == SPECIAL_DEATH) {
        return remove_marked_character(host, ch, cmd, arg, callflag, wtl);
    }
    return 0;
}

/* Removes record.object_to_remove, and lets the command go on. */
SPECIAL(remove_marked_object)
{
    if (record.object_to_remove) {
        obj_data* marked = record.object_to_remove;
        record.object_to_remove = nullptr;
        extract_obj(marked);
    }
    return 0;
}

/* Removes whoever flees past it, and lets the flight go on. */
SPECIAL(remove_actor_fleeing)
{
    if (callflag == SPECIAL_COMMAND && cmd == FLEE_COMMAND + 1) {
        extract_char(ch);
    }
    return 0;
}

/* Removes whoever yells within earshot, and lets the yell go on. */
SPECIAL(remove_actor_yelling)
{
    if (callflag == SPECIAL_COMMAND && cmd == CMD_YELL) {
        extract_char(ch);
    }
    return 0;
}

/* On the far side of a door, removes whoever knocks on it. */
SPECIAL(remove_actor_knocking)
{
    if (cmd == CMD_KNOCK) {
        extract_char(ch);
    }
    return 0;
}

/* On the far side of a door, takes whoever knocks on it through to that side. */
SPECIAL(take_knocker_through)
{
    if (cmd == CMD_KNOCK) {
        char_from_room(ch);
        char_to_room(ch, SECOND_ROOM);
    }
    return 0;
}

/* Removes whoever damages its mob, and lets the damage go on. */
SPECIAL(remove_attacker)
{
    if (callflag == SPECIAL_DAMAGE) {
        extract_char(ch);
    }
    return 0;
}

/* On its own mob's turn, removes record.character_to_remove and then the mob itself. */
SPECIAL(remove_marked_then_self)
{
    if (callflag == SPECIAL_SELF) {
        remove_marked_character(host, ch, cmd, arg, callflag, wtl);
        extract_char(host);
    }
    return 0;
}

class SpecialProcedureTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        link_second_room();
        /* Made at boot; damage and deaths ask them. Making them again keeps the ones there are. */
        game_rules::big_brother::create(weather_info, &world);
        game_timer::skill_timer::create(weather_info, &world);
        record = procedure_record {};
        m_saved_mob_index = mob_index;
        m_saved_top_of_mobt = top_of_mobt;
        mob_index = m_mob_prototypes;
        top_of_mobt = MOB_PROTOTYPE_SLOTS - 1;
        for (int room = 0; room <= SECOND_ROOM; ++room) {
            m_saved_room_procedures[room] = world[room].funct;
            m_saved_room_flags[room] = world[room].room_flags;
            world[room].funct = nullptr;
            world[room].room_flags = 0;
        }
    }

    void TearDown() override
    {
        /* The mobs still in the game are extracted with their prototype entries in place. */
        ScriptedRoomFixture::TearDown();
        for (char_prof_data*& profs : m_mob_profs)
            RELEASE(profs);
        mob_index = m_saved_mob_index;
        top_of_mobt = m_saved_top_of_mobt;
        for (int room = 0; room <= SECOND_ROOM; ++room) {
            world[room].funct = m_saved_room_procedures[room];
            world[room].room_flags = m_saved_room_flags[room];
        }
        record = procedure_record {};
    }

    /* Gives <mob> a prototype entry of its own running <procedure> (none for null), as a mob read
     * from the world files has. The game then no longer frees the mob's strings or profession
     * block, so the test keeps them. */
    void give_prototype(char_data* mob, special_func procedure)
    {
        if (m_used_prototypes >= MOB_PROTOTYPE_SLOTS) {
            ADD_FAILURE() << "more than " << MOB_PROTOTYPE_SLOTS << " mobs with prototypes";
            return;
        }
        const int slot = m_used_prototypes++;
        m_mob_prototypes[slot].func = procedure;
        mob->nr = slot;
        if (procedure) {
            SET_BIT(MOB_FLAGS(mob), MOB_SPEC);
        }
        m_mob_names.emplace_back(mob->player.name);
        RELEASE(mob->player.name);
        RELEASE(mob->player.short_descr);
        mob->player.name = m_mob_names.back().data();
        mob->player.short_descr = m_mob_names.back().data();
        m_mob_profs.push_back(mob->profs);
    }

    /* A mob in <room> with a prototype entry running <procedure>, and the moves to walk anywhere. */
    char_data* make_mob_with(const char* keywords, special_func procedure, int room = 0)
    {
        char_data* mob = make_mob(keywords, 0, room);
        mob->abilities.move = PLENTY_OF_MOVES;
        mob->tmpabilities.move = PLENTY_OF_MOVES;
        give_prototype(mob, procedure);
        return mob;
    }

    /* An object of the prototype with <vnum>, whose prototype runs <procedure>; not placed. */
    obj_data* make_object_with(int vnum, special_func procedure)
    {
        obj_data* object = make_object(vnum, 0);
        obj_index[object->item_number].func = procedure;
        return object;
    }

    /* A player character in <room> with no connection, made on the heap so that the game can
     * free it; TearDown extracts it with the mobs if it is still in the game. */
    char_data* make_player_character(const char* name, int room = 0)
    {
        char_data* player = owned_character().release();
        new (&player->damage_details) player_damage_details();
        player->player.name = str_dup(name);
        player->player.race = RACE_HUMAN;
        player->player.level = 10;
        player->tmpabilities.str = 18;
        player->tmpabilities.dex = 18;
        player->tmpabilities.intel = 18;
        player->tmpabilities.move = PLENTY_OF_MOVES;
        player->specials.position = POSITION_STANDING;
        player->next = character_list;
        character_list = player;
        register_pc_char(player);
        char_to_room(player, room);
        return player;
    }

    /* Makes <victim> one that mental attacks reach and that never resists one: full perception,
     * no willpower and a mind that can be fathomed. */
    static void open_to_mental_attack(char_data* victim)
    {
        victim->specials2.perception = 100;
        victim->points.willpower = 0;
        victim->player.bodytype = 1;
    }

    /* Makes <mob> a wimpy one whose strength turns critical, and so makes it flee, at the first
     * point a mental attack drains, while every other stat stays far from zero. */
    static void make_critical_on_strength_loss(char_data* mob)
    {
        SET_BIT(MOB_FLAGS(mob), MOB_WIMPY);
        /* A move compares each stat with its base, so the bases are set as well. */
        mob->abilities.str = SAFE_STAT;
        mob->abilities.dex = SAFE_STAT;
        mob->abilities.con = SAFE_STAT;
        mob->tmpabilities.str = LOWEST_SAFE_STAT;
        mob->tmpabilities.intel = SAFE_STAT;
        mob->tmpabilities.wil = SAFE_STAT;
        mob->tmpabilities.dex = SAFE_STAT;
        mob->tmpabilities.con = SAFE_STAT;
        mob->tmpabilities.lea = SAFE_STAT;
    }

    /* Thuringwethil, beaten, open to mental attack: any call of her procedure, a mental attack's
     * included, removes her. */
    char_data* make_beaten_thuringwethil()
    {
        char_data* vampire = make_mob_with("vampire", thuringwethil);
        vampire->tmpabilities.hit = THURINGWETHIL_BEATEN_HIT_POINTS;
        open_to_mental_attack(vampire);
        return vampire;
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

    /* Moves <walker> north, as the interpreter does for the north command. */
    static void walk_north(char_data* walker) { do_move(walker, empty_argument(), nullptr, NORTH + 1, 0); }

    /* Offers <ch>'s command <cmd> to the procedures here, as the interpreter does. */
    static int offer_command(char_data* ch, int cmd = NORTH + 1)
    {
        return special(ch, cmd, empty_argument(), SPECIAL_COMMAND, nullptr);
    }

    /* A writable empty argument for the commands. */
    static char* empty_argument()
    {
        static char empty[1] = "";
        empty[0] = '\0';
        return empty;
    }

    /* Whether record_offer or intercept_command ran for <host>. */
    static bool was_offered(const void* host)
    {
        const std::vector<const void*>& offered = record.offered_hosts;
        return std::find(offered.begin(), offered.end(), host) != offered.end();
    }

    /* Puts <first> and <second> in a fight with each other. */
    static void start_fight(char_data* first, char_data* second)
    {
        set_fighting(first, second);
        set_fighting(second, first);
    }

private:
    // The prototype entries give_prototype hands out, in place of the game's mob_index.
    index_data m_mob_prototypes[MOB_PROTOTYPE_SLOTS] {};
    // How many of m_mob_prototypes give_prototype has handed out.
    int m_used_prototypes = 0;
    // The names of the mobs with prototype entries, which the game no longer frees; a deque, so
    // earlier names never move.
    std::deque<std::string> m_mob_names;
    // The profession blocks clear_char gave the mobs with prototype entries, which free_char then
    // leaves alone as it leaves a prototype's; released by TearDown.
    std::vector<char_prof_data*> m_mob_profs;
    // The game's mob prototype entries and their count, put back by TearDown.
    index_data* m_saved_mob_index = nullptr;
    int m_saved_top_of_mobt = 0;
    // The procedures and flags of rooms 0 and SECOND_ROOM before the test, put back by TearDown.
    special_func m_saved_room_procedures[SECOND_ROOM + 1] {};
    long m_saved_room_flags[SECOND_ROOM + 1] {};
};

} // namespace
