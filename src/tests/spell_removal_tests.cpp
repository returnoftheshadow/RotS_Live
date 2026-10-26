/* Room spells (fireball, earthquake) whose own hits remove a character the spell still goes on to use:
 * the caster, the primary victim, or whoever is next in the room. A damage script that removes
 * someone (EXTRACT_CHAR) stands in for a death; the spell stops touching whoever was removed, and
 * otherwise plays out as before. */
#include "scripted_room_fixture.h"

#include "../big_brother.h"
#include "../skill_timer.h"
#include "../spells.h"

extern struct weather_data weather_info;

using namespace scripted_room;

namespace {

/* Enough queued rolls for every draw one spell makes in these rooms. */
constexpr int QUEUED_ROLLS = 400;
/* A roll that makes number(0, 1) come out 0, so every occupant of a cracking room falls. */
constexpr double LOW_ROLL = 0.25;
/* A roll under the fireball's 0.2 splash chance, so the splash reaches every bystander. */
constexpr double SPLASH_ROLL = 0.1;

class SpellRemovalTest : public ScriptedRoomFixture {
protected:
    void SetUp() override
    {
        ScriptedRoomFixture::SetUp();
        /* Made at boot; damage asks them. Making them again keeps the ones there are. */
        game_rules::big_brother::create(weather_info, &world);
        game_timer::skill_timer::create(weather_info, &world);
        m_saved_sector_type = world[0].sector_type;
        m_saved_room_flags = world[0].room_flags;
        world[0].sector_type = SECT_FIELD;
        world[0].room_flags = 0;
    }

    void TearDown() override
    {
        world[0].sector_type = m_saved_sector_type;
        world[0].room_flags = m_saved_room_flags;
        ScriptedRoomFixture::TearDown();
    }

    /* Takes the player out of room 0, so the spells' occupants are only the test mobs, in the
     * order they were made. */
    void clear_the_room_of_the_player() { char_from_room(&m_player); }

    /* Makes the player a fire-specialised mage in room 0 who survives any hit here. */
    void make_player_a_fire_mage()
    {
        m_player.profs->specialization = game_types::PS_Fire;
        m_player.abilities.hit = MOB_HIT_POINTS;
        m_player.tmpabilities.hit = MOB_HIT_POINTS;
    }

    /* Queues <count> copies of <roll> for number(). */
    static void queue_rolls(double roll, int count = QUEUED_ROLLS)
    {
        for (int index = 0; index < count; ++index) {
            push_test_random_value(roll);
        }
    }

    /* Casts <spell> as <caster> at <victim>. */
    static void cast(void (*spell)(char_data*, char*, int, char_data*, obj_data*, int, int),
        char_data* caster, char_data* victim)
    {
        static char empty[1] = "";
        spell(caster, empty, SPELL_TYPE_SPELL, victim, nullptr, 0, 0);
    }

    /* Makes script SCRIPT_VNUM a damage script that removes ch<parameter>: SCRIPT_PARAM_CH1 for
     * the one hurt, SCRIPT_PARAM_CH2 for whoever hurt it. */
    void install_removal_on_damage(int parameter)
    {
        install_script(ON_DAMAGE, { { SCRIPT_EXTRACT_CHAR, nullptr, { parameter } } });
    }

private:
    // Room 0's sector and flags before the test, put back by TearDown.
    int m_saved_sector_type = 0;
    long m_saved_room_flags = 0;
};

} // namespace

/* ---- fireball ---- */

TEST_F(SpellRemovalTest, AFireballWhoseHitRemovesTheCasterSplashesNobody)
{
    install_removal_on_damage(SCRIPT_PARAM_CH2);
    clear_the_room_of_the_player();
    char_data* caster = make_mob("mage", 0);
    char_data* victim = make_mob("victim", SCRIPT_VNUM);
    char_data* bystander = make_mob("bystander", 0);
    queue_rolls(SPLASH_ROLL);

    cast(spell_fireball, caster, victim);

    EXPECT_FALSE(is_in_game(caster));
    EXPECT_EQ(bystander->tmpabilities.hit, MOB_HIT_POINTS);
}

/* A fire mage's splash spares the room when the primary victim is friendly; a victim its hit
 * removed is still judged as it was when the fireball struck. */
TEST_F(SpellRemovalTest, AFireMagesSplashSparesAFriendlyVictimsRoomAfterTheHitRemovesTheVictim)
{
    install_removal_on_damage(SCRIPT_PARAM_CH1);
    make_player_a_fire_mage();
    char_data* victim = make_mob("victim", SCRIPT_VNUM);
    char_data* bystander = make_mob("bystander", 0);
    queue_rolls(SPLASH_ROLL);

    cast(spell_fireball, &m_player, victim);

    EXPECT_FALSE(is_in_game(victim));
    EXPECT_EQ(bystander->tmpabilities.hit, MOB_HIT_POINTS);
}

TEST_F(SpellRemovalTest, AFireMagesSplashSparesAFriendlyVictimsRoom)
{
    make_player_a_fire_mage();
    char_data* victim = make_mob("victim", 0);
    char_data* bystander = make_mob("bystander", 0);
    queue_rolls(SPLASH_ROLL);

    cast(spell_fireball, &m_player, victim);

    EXPECT_LT(victim->tmpabilities.hit, MOB_HIT_POINTS);
    EXPECT_EQ(bystander->tmpabilities.hit, MOB_HIT_POINTS);
}

TEST_F(SpellRemovalTest, AFireballSplashesEveryBystanderWhenNobodyIsRemoved)
{
    clear_the_room_of_the_player();
    char_data* caster = make_mob("mage", 0);
    char_data* victim = make_mob("victim", 0);
    char_data* first = make_mob("first", 0);
    char_data* second = make_mob("second", 0);
    queue_rolls(SPLASH_ROLL);

    cast(spell_fireball, caster, victim);

    EXPECT_LT(victim->tmpabilities.hit, MOB_HIT_POINTS);
    EXPECT_LT(first->tmpabilities.hit, MOB_HIT_POINTS);
    EXPECT_LT(second->tmpabilities.hit, MOB_HIT_POINTS);
}

/* The bystander's splash removes the caster: the rest of the room is not splashed. */
TEST_F(SpellRemovalTest, AFireballSplashThatRemovesTheCasterStopsThere)
{
    install_removal_on_damage(SCRIPT_PARAM_CH2);
    clear_the_room_of_the_player();
    char_data* caster = make_mob("mage", 0);
    char_data* victim = make_mob("victim", 0);
    char_data* remover = make_mob("remover", SCRIPT_VNUM);
    char_data* last = make_mob("last", 0);
    queue_rolls(SPLASH_ROLL);

    cast(spell_fireball, caster, victim);

    EXPECT_FALSE(is_in_game(caster));
    EXPECT_TRUE(is_in_game(remover));
    EXPECT_EQ(last->tmpabilities.hit, MOB_HIT_POINTS);
}

/* ---- earthquake ---- */

/* Indoors the ground does not crack; the tremor stops at the occupant whose hit removed the
 * caster. */
TEST_F(SpellRemovalTest, AnEarthquakeWhoseHitRemovesTheCasterShakesNobodyElse)
{
    install_removal_on_damage(SCRIPT_PARAM_CH2);
    world[0].room_flags = INDOORS;
    clear_the_room_of_the_player();
    char_data* caster = make_mob("mage", 0);
    char_data* remover = make_mob("remover", SCRIPT_VNUM);
    char_data* last = make_mob("last", 0);
    queue_rolls(LOW_ROLL);

    cast(spell_earthquake, caster, nullptr);

    EXPECT_FALSE(is_in_game(caster));
    EXPECT_TRUE(is_in_game(remover));
    EXPECT_EQ(last->tmpabilities.hit, MOB_HIT_POINTS);
}

/* The caster falls first, and its own fall removes it: nobody falls after it. */
TEST_F(SpellRemovalTest, ACasterRemovedByItsOwnFallLetsNobodyElseFall)
{
    install_removal_on_damage(SCRIPT_PARAM_CH1);
    link_second_room();
    clear_the_room_of_the_player();
    char_data* caster = make_mob("mage", SCRIPT_VNUM);
    char_data* bystander = make_mob("bystander", 0);
    queue_rolls(LOW_ROLL);

    cast(spell_earthquake, caster, nullptr);

    EXPECT_FALSE(is_in_game(caster));
    EXPECT_EQ(bystander->in_room, 0);
}

TEST_F(SpellRemovalTest, AnEarthquakeCrackDropsEveryoneWhoLosesTheirBalance)
{
    link_second_room();
    clear_the_room_of_the_player();
    char_data* caster = make_mob("mage", 0);
    char_data* bystander = make_mob("bystander", 0);
    queue_rolls(LOW_ROLL);

    cast(spell_earthquake, caster, nullptr);

    EXPECT_EQ(caster->in_room, SECOND_ROOM);
    EXPECT_EQ(bystander->in_room, SECOND_ROOM);
    EXPECT_LT(bystander->tmpabilities.hit, MOB_HIT_POINTS);
}
