// The affect ticks: a character's affects (affect_update_person), the list of affected characters
// and rooms (affect_update), a room's affects (affect_update_room) and the fame-war bonus
// (do_fame_war_bonuses). A tick body can free affects, characters and list entries that the walk
// has not reached yet; the walk goes on without touching them.
//
// A test that needs a roll to go one way queues it with push_test_random_value.
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../limits.h"
#include "../pkill.h"
#include "../skill_timer.h"
#include "../spells.h"

#include <vector>

void affect_update_person(struct char_data* character, int mode);
void affect_update();
void affect_update_room(struct room_data* room);
void do_fame_war_bonuses(struct char_data* character);
void assign_pk_bonuses(struct char_data* character, int profession, int tier, bool adding);

extern struct weather_data weather_info;
extern universal_list* affected_list;
extern struct index_data* mob_index;
extern struct char_data* mob_proto;
extern int top_of_mobt;

using namespace scripted_room;

namespace {

// How long the affects the tests give last, in hours.
constexpr int AFFECT_HOURS = 5;
// The fame-war tier the dropping player held: its bonus is what must go.
constexpr int HELD_FAME_WAR_TIER = 3;
// The warrior level that makes the player's fame-war bonus a warrior's.
constexpr int WARRIOR_LEVEL = 20;
// How hard a mob after the update's end was attacked; a tick that reaches the mob lowers it.
constexpr int LATER_ATTACKED_LEVEL = 10;
// affect_update_person's mode for the hourly pass, as against fast_update's.
constexpr int HOURLY_AFFECT_UPDATE = 0;
// A sanctuary's strength (its modifier), which every blow wears down: a whole one, and one below
// zero, which breaks at the next blow.
constexpr int SANCTUARY_STRENGTH = 100;
constexpr int BREAKING_SANCTUARY_STRENGTH = -1;
// Hit points far enough below zero for any blow or blaze to kill.
constexpr int DYING_HIT_POINTS = -9;
// How hard the blaze the tests cast burns.
constexpr int BLAZE_STRENGTH = 10;
// affect_update_room's rolls. Its mist roll (number(1, 100)) moves a mist below 75 and keeps it
// from 75 on; a moving mist's direction roll (number(0, NUM_OF_DIRS - 1)) of 0 is north; a room
// spell's roll (number(0, 12)) of 0 strikes an occupant.
constexpr double MIST_MOVES_ROLL = 0.0;
constexpr double MIST_STAYS_ROLL = 0.99;
constexpr double NORTH_ROLL = 0.0;
constexpr double ROOM_SPELL_STRIKES_ROLL = 0.0;
// The mob table entry make_dying_mob's mobs are made from, the test table's only one.
constexpr int DYING_MOB_NUMBER = 0;
// A player index that is in no player table, which makes the player unranked.
constexpr int NO_PLAYER_INDEX = -1;
// How long a fame-war affect lasts, in hours, as do_fame_war_bonuses gives it.
constexpr int FAME_WAR_HOURS = 100;
// The ranking do_fame_war_bonuses records for an unranked player: pkill's rank plus one.
constexpr int UNRANKED_RANKING = PKILL_UNRANKED + 1;

// Runs the affect ticks with an empty affected_list, rooms 0 and 1 without affects, and a
// one-entry mob table for the mobs whose death must leave a corpse.
class AffectTickTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        link_second_room();
        // Made at boot; damage() asks them. Making them again keeps the ones there are.
        game_rules::big_brother::create(weather_info, &world);
        game_timer::skill_timer::create(weather_info, &world);
        m_saved_affected_list = affected_list;
        affected_list = nullptr;
        m_saved_mob_index = mob_index;
        m_saved_mob_proto = mob_proto;
        m_saved_top_of_mobt = top_of_mobt;
        mob_index = m_dying_mob_index;
        mob_proto = nullptr;
        top_of_mobt = DYING_MOB_NUMBER;
        for (int room = 0; room <= SECOND_ROOM; ++room) {
            m_saved_room_affects[room] = world[room].affected;
            m_saved_room_flags[room] = world[room].room_flags;
            world[room].affected = nullptr;
        }
    }

    void TearDown() override
    {
        for (int room = 0; room <= SECOND_ROOM; ++room) {
            while (world[room].affected) {
                affect_remove_room(&world[room], world[room].affected);
            }
        }

        while (m_player.affected) {
            affect_remove(&m_player, m_player.affected);
        }
        ScriptedRoomFixture::TearDown();
        for (char_prof_data*& profs : m_dying_mob_profs) {
            RELEASE(profs);
        }

        // Every affect is gone, and with it every entry of affected_list.
        EXPECT_EQ(nullptr, affected_list);
        affected_list = m_saved_affected_list;
        mob_index = m_saved_mob_index;
        mob_proto = m_saved_mob_proto;
        top_of_mobt = m_saved_top_of_mobt;
        for (int room = 0; room <= SECOND_ROOM; ++room) {
            world[room].affected = m_saved_room_affects[room];
            world[room].room_flags = m_saved_room_flags[room];
        }
    }

    // Gives <character> an affect of <type> for AFFECT_HOURS with <modifier>, changing nothing
    // else, at the head of its list.
    static void give_affect(char_data* character, int type, int modifier = 0)
    {
        affected_type affect {};
        affect.type = type;
        affect.duration = AFFECT_HOURS;
        affect.modifier = modifier;
        affect.location = APPLY_NONE;
        affect_to_char(character, &affect);
    }

    // Gives <room> a room spell <spell> for AFFECT_HOURS with <modifier>.
    static void give_room_spell(int room, int spell, int modifier)
    {
        affected_type affect {};
        affect.type = ROOMAFF_SPELL;
        affect.duration = AFFECT_HOURS;
        affect.modifier = modifier;
        affect.location = spell;
        affect_to_room(&world[room], &affect);
    }

    // A mob in <room>, running <script_virtual_number> (none for 0), made from a prototype, as a
    // mob must be for its death to make a corpse. Its keywords are <keywords>, which must outlive
    // it.
    char_data* make_dying_mob(char* keywords, int script_virtual_number, int room)
    {
        char_data* mob = make_mob(keywords, script_virtual_number, room);
        // A mob made from a prototype shares its strings, so free_char leaves them.
        RELEASE(mob->player.name);
        RELEASE(mob->player.short_descr);
        mob->player.name = keywords;
        mob->player.short_descr = keywords;
        m_dying_mob_profs.push_back(mob->profs); // nor its profession block: TearDown frees it
        mob->nr = DYING_MOB_NUMBER;
        m_dying_mob_index[DYING_MOB_NUMBER].number++;
        return mob;
    }

    // A mob in <room> with nobody else there, the fixture's player taken out of room 0.
    char_data* make_lone_mob(const char* keywords, int room)
    {
        world[0].people = nullptr;
        return make_mob(keywords, 0, room);
    }

private:
    // affected_list before the test, which starts with an empty one; put back by TearDown.
    universal_list* m_saved_affected_list = nullptr;
    // The affects and flags of rooms 0 and 1 before the test, which starts them with none.
    affected_type* m_saved_room_affects[SECOND_ROOM + 1] {};
    long m_saved_room_flags[SECOND_ROOM + 1] {};
    // The mob prototype index and prototypes before the test, put back by TearDown.
    index_data* m_saved_mob_index = nullptr;
    char_data* m_saved_mob_proto = nullptr;
    int m_saved_top_of_mobt = 0;
    // The one prototype index entry make_dying_mob's mobs are made from.
    index_data m_dying_mob_index[DYING_MOB_NUMBER + 1] {};
    // The profession blocks of the mobs make_dying_mob gave a prototype number, which free_char
    // then leaves to the prototype; released by TearDown.
    std::vector<char_prof_data*> m_dying_mob_profs;
};

} // namespace

// ---- one character's affects ----

TEST_F(AffectTickTest, EveryAffectLosesAnHourOnItsPhase)
{
    char_data* mob = make_lone_mob("mob", 0);
    give_affect(mob, SPELL_ARMOR);
    give_affect(mob, SPELL_SANCTUARY, SANCTUARY_STRENGTH);

    affect_update_person(mob, HOURLY_AFFECT_UPDATE);

    EXPECT_EQ(AFFECT_HOURS - 1, affected_by_spell(mob, SPELL_ARMOR)->duration);
    EXPECT_EQ(AFFECT_HOURS - 1, affected_by_spell(mob, SPELL_SANCTUARY)->duration);
}

// The poison's damage breaks the sanctuary next in the list; the armor after it still ticks.
TEST_F(AffectTickTest, APoisonTickThatBreaksASanctuaryGoesOnToTheAffectAfterIt)
{
    char_data* mob = make_lone_mob("mob", 0);
    give_affect(mob, SPELL_ARMOR);
    give_affect(mob, SPELL_SANCTUARY, BREAKING_SANCTUARY_STRENGTH);
    give_affect(mob, SPELL_POISON);
    const int hit_points_before = GET_HIT(mob);

    affect_update_person(mob, HOURLY_AFFECT_UPDATE);

    EXPECT_LT(GET_HIT(mob), hit_points_before) << "the poison hurt the mob";
    EXPECT_EQ(nullptr, affected_by_spell(mob, SPELL_SANCTUARY));
    ASSERT_NE(nullptr, affected_by_spell(mob, SPELL_ARMOR));
    EXPECT_EQ(AFFECT_HOURS - 1, affected_by_spell(mob, SPELL_ARMOR)->duration);
    EXPECT_EQ(AFFECT_HOURS - 1, affected_by_spell(mob, SPELL_POISON)->duration);
}

// The poison's damage drains the last of the mob's mana into the shield next in the list, which
// then goes; the armor after it still ticks.
TEST_F(AffectTickTest, APoisonTickThatDrainsAShieldGoesOnToTheAffectAfterIt)
{
    char_data* mob = make_lone_mob("mob", 0);
    mob->tmpabilities.mana = 0;
    give_affect(mob, SPELL_ARMOR);
    give_affect(mob, SPELL_SHIELD);
    give_affect(mob, SPELL_POISON);

    affect_update_person(mob, HOURLY_AFFECT_UPDATE);

    EXPECT_EQ(nullptr, affected_by_spell(mob, SPELL_SHIELD));
    ASSERT_NE(nullptr, affected_by_spell(mob, SPELL_ARMOR));
    EXPECT_EQ(AFFECT_HOURS - 1, affected_by_spell(mob, SPELL_ARMOR)->duration);
}

// ---- the list of affected characters and rooms ----

// A blaze's tick kills the one mob standing in it, whose entry is next on affected_list; the
// entry after that, another mob's, is still updated.
TEST_F(AffectTickTest, ARoomTickThatKillsAnOccupantGoesOnToTheNextEntry)
{
    char_data* bystander = make_lone_mob("bystander", SECOND_ROOM);
    give_affect(bystander, SPELL_ARMOR);
    char victim_keywords[] = "victim";
    char_data* victim = make_dying_mob(victim_keywords, 0, 0);
    give_affect(victim, SPELL_ARMOR);
    victim->tmpabilities.hit = DYING_HIT_POINTS;
    give_room_spell(0, SPELL_BLAZE, BLAZE_STRENGTH);
    ASSERT_EQ(TARGET_ROOM, affected_list->type) << "the room's entry is walked first";
    // The room's mists check (there is no mist), then the blaze's roll for its one occupant.
    push_test_random_value(MIST_MOVES_ROLL);
    push_test_random_value(ROOM_SPELL_STRIKES_ROLL);

    affect_update();

    EXPECT_FALSE(is_in_game(victim));
    ASSERT_NE(nullptr, affected_by_spell(bystander, SPELL_ARMOR));
    EXPECT_EQ(AFFECT_HOURS - 1, affected_by_spell(bystander, SPELL_ARMOR)->duration);
}

// ---- a room's affects ----

// The blaze kills the first occupant, whose death script makes the next one vanish: the blaze
// touches nobody after that.
TEST_F(AffectTickTest, ARoomTickStopsWhenADeathFreesTheNextOccupant)
{
    install_scripts(
        { { SCRIPT_VNUM, { { ON_DIE, { { SCRIPT_DO_SAY, "vanish", { SCRIPT_PARAM_CH1 } } } } } },
            { OTHER_SCRIPT_VNUM,
                { { ON_HEAR_SAY,
                    { { SCRIPT_IF_STR_CONTAINS, "VANISH", { SCRIPT_PARAM_STR1 } },
                        { SCRIPT_BEGIN, nullptr, {} },
                        { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } },
                        { SCRIPT_END, nullptr, {} } } } } } });
    world[0].people = nullptr;
    char victim_keywords[] = "victim";
    char_data* victim = make_dying_mob(victim_keywords, SCRIPT_VNUM, 0);
    char_data* listener = make_mob("listener", OTHER_SCRIPT_VNUM, 0);
    ASSERT_EQ(listener, victim->next_in_room);
    victim->tmpabilities.hit = DYING_HIT_POINTS;
    give_room_spell(0, SPELL_BLAZE, BLAZE_STRENGTH);
    // The room's mists check (there is no mist), then the blaze's roll for the victim.
    push_test_random_value(MIST_MOVES_ROLL);
    push_test_random_value(ROOM_SPELL_STRIKES_ROLL);

    affect_update_room(&world[0]);

    EXPECT_FALSE(is_in_game(victim));
    EXPECT_FALSE(is_in_game(listener));
}

// ---- the hourly update of every character ----

// A poisoned mob's death sets off a script that removes the character after it on the list: the
// update ends there with a log line, and the one after that waits, untouched, for the next
// update.
TEST_F(AffectTickTest, AnUpdateEndsWhenAPoisonDeathFreesTheNextCharacter)
{
    install_scripts(
        { { SCRIPT_VNUM, { { ON_DIE, { { SCRIPT_DO_SAY, "vanish", { SCRIPT_PARAM_CH1 } } } } } },
            { OTHER_SCRIPT_VNUM,
                { { ON_HEAR_SAY,
                    { { SCRIPT_IF_STR_CONTAINS, "VANISH", { SCRIPT_PARAM_STR1 } },
                        { SCRIPT_BEGIN, nullptr, {} },
                        { SCRIPT_EXTRACT_CHAR, nullptr, { SCRIPT_PARAM_CH1 } },
                        { SCRIPT_END, nullptr, {} } } } } } });
    world[0].people = nullptr;
    // make_mob puts each mob at the head of the list: the last made is updated first. Out of
    // earshot, the later mob does not hear the dying one.
    char_data* later = make_mob("later", 0, SECOND_ROOM);
    later->specials.attacked_level = LATER_ATTACKED_LEVEL;
    char_data* listener = make_mob("listener", OTHER_SCRIPT_VNUM, 0);
    char victim_keywords[] = "victim";
    char_data* victim = make_dying_mob(victim_keywords, SCRIPT_VNUM, 0);
    ASSERT_EQ(victim, character_list);
    ASSERT_EQ(listener, victim->next);
    ASSERT_EQ(later, listener->next);
    // Poisoned by the flag alone, as gear poisons, and so hurt that the poison's blow kills.
    SET_BIT(victim->specials.affected_by, AFF_POISON);
    victim->tmpabilities.hit = DYING_HIT_POINTS;
    // The update brings intelligence back to its base first: at the base of 0 the mob is too stupid
    // to say a word as it dies.
    victim->abilities.intel = victim->tmpabilities.intel;

    testing::internal::CaptureStderr();
    point_update();
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(is_in_game(victim));
    EXPECT_FALSE(is_in_game(listener));
    ASSERT_TRUE(is_in_game(later));
    EXPECT_EQ(LATER_ATTACKED_LEVEL, later->specials.attacked_level);
    EXPECT_NE(std::string::npos,
        logged.find("SYSERR: point_update: the next character was freed; the rest wait for the "
                    "next tick."))
        << logged;
}

TEST_F(AffectTickTest, AMistThatDriftsLeavesItsRoomAndArrivesNextDoor)
{
    world[0].people = nullptr;
    give_room_spell(0, SPELL_MIST_OF_BAAZUNGA, 0);
    // The mist moves, north, which leads to the second room.
    push_test_random_value(MIST_MOVES_ROLL);
    push_test_random_value(NORTH_ROLL);

    affect_update_room(&world[0]);

    EXPECT_EQ(nullptr, room_affected_by_spell(&world[0], SPELL_MIST_OF_BAAZUNGA));
    const affected_type* arrived
        = room_affected_by_spell(&world[SECOND_ROOM], SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(nullptr, arrived);
    EXPECT_EQ(AFFECT_HOURS - 1, arrived->duration);
}

TEST_F(AffectTickTest, AMistThatStaysLosesAnHour)
{
    world[0].people = nullptr;
    give_room_spell(0, SPELL_MIST_OF_BAAZUNGA, 0);
    push_test_random_value(MIST_STAYS_ROLL);

    affect_update_room(&world[0]);

    const affected_type* mist = room_affected_by_spell(&world[0], SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(nullptr, mist);
    EXPECT_EQ(AFFECT_HOURS - 1, mist->duration);
    EXPECT_EQ(nullptr, room_affected_by_spell(&world[SECOND_ROOM], SPELL_MIST_OF_BAAZUNGA));
}

// ---- the fame-war bonus ----

// A player whose rank is no longer a ranked one loses the bonus and the affect, and gets none
// back.
TEST_F(AffectTickTest, APlayerWhoDropsOutOfTheRanksKeepsNoFameWarBonus)
{
    m_player.player_index = NO_PLAYER_INDEX;
    m_player.profs->prof_level[PROF_WARRIOR] = WARRIOR_LEVEL;
    m_player.player.ranking = HELD_FAME_WAR_TIER;
    const int base_hit_points = m_player.constabilities.hit;
    const int base_constitution = m_player.constabilities.con;
    const int base_offense = SET_OB(&m_player);
    assign_pk_bonuses(&m_player, PROF_WARRIOR, HELD_FAME_WAR_TIER, true);
    affected_type fame_war {};
    fame_war.type = SPELL_FAME_WAR;
    fame_war.duration = FAME_WAR_HOURS;
    fame_war.modifier = HELD_FAME_WAR_TIER;
    fame_war.location = APPLY_NONE;
    affect_to_char(&m_player, &fame_war);
    ASSERT_GT(m_player.constabilities.hit, base_hit_points) << "the bonus is on";

    do_fame_war_bonuses(&m_player);

    EXPECT_EQ(nullptr, affected_by_spell(&m_player, SPELL_FAME_WAR));
    EXPECT_EQ(base_hit_points, m_player.constabilities.hit);
    EXPECT_EQ(base_constitution, m_player.constabilities.con);
    EXPECT_EQ(base_offense, SET_OB(&m_player));
    EXPECT_FALSE(output_has("The power of war"));
}

// An unranked player with no fame-war affect is given none, and its rank is recorded.
TEST_F(AffectTickTest, AnUnrankedPlayerWithoutTheAffectIsGivenNone)
{
    m_player.player_index = NO_PLAYER_INDEX;
    m_player.player.ranking = PKILL_UNRANKED;
    m_player.profs->prof_level[PROF_WARRIOR] = WARRIOR_LEVEL;

    do_fame_war_bonuses(&m_player);

    EXPECT_EQ(nullptr, affected_by_spell(&m_player, SPELL_FAME_WAR));
    EXPECT_EQ(UNRANKED_RANKING, m_player.player.ranking);
}
