#include "../damage_meters.h"
#include "../db.h"
#include "../owned_character.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "allocated_bytes.h"

#include <gtest/gtest.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

extern struct char_data* combat_list;
extern struct char_data* character_list;
extern struct char_data* mob_proto;
extern struct index_data* mob_index;
extern int top_of_mobt;

namespace {
// The damage of one recorded hit.
constexpr int HIT_DAMAGE = 10;
// The combat time of the first round a test ticks.
constexpr float FIRST_ROUND_SECONDS = 1.25f;
// The combat time of the second round a test ticks.
constexpr float SECOND_ROUND_SECONDS = 2.5f;
// The name of the player a test reports on.
constexpr char PLAYER_NAME[] = "Hurin";

// The report line that shows seconds of combat time, written as the damage report writes it.
std::string combat_time_text(float seconds)
{
    std::ostringstream writer;
    writer << "Combat Time: " << std::fixed << std::setprecision(2) << seconds << "s";
    return writer.str();
}

// The report line that shows hit_count hits totalling total_damage.
std::string count_and_total_text(int hit_count, int total_damage)
{
    return "Count: " + std::to_string(hit_count) + ", Total: " + std::to_string(total_damage);
}

// The whole report of a character named name that has recorded no damage.
std::string no_damage_report(std::string_view name)
{
    std::string report(name);
    report += " has not recorded any damage dealt.\n";
    return report;
}

// Two player fighters listed on combat_list for one test, which the context restores afterwards.
class CombatListContext {
public:
    CombatListContext()
    {
        first_fighter->player.name = str_dup("first_fighter");
        second_fighter->player.name = str_dup("second_fighter");

        first_fighter->next_fighting = second_fighter;
        second_fighter->next_fighting = nullptr;
        previous_combat_list = combat_list;
        combat_list = first_fighter;
    }

    ~CombatListContext() { combat_list = previous_combat_list; }

    // Makes second_fighter an NPC, as a mob from the world files is.
    void make_second_fighter_a_mob()
    {
        second_fighter->specials2.act = MOB_ISNPC;
        second_fighter->player.short_descr = second_fighter_short_description;
    }

    // The head of combat_list.
    owned_character first_fighter;
    // The fighter after first_fighter on combat_list.
    owned_character second_fighter;

private:
    // Storage for second_fighter's short description once it is a mob, which its reports print.
    char second_fighter_short_description[16] = "a cave troll";
    // The combat_list in place before this context replaced it.
    char_data* previous_combat_list = nullptr;
};
} // namespace

TEST(DamageMeters, AddsTheGivenSecondsToEveryFightersPersonalMeter)
{
    CombatListContext context;
    // A personal report shows the combat time only once some damage is recorded.
    ensure_damage_meter(*context.first_fighter).add_damage(TYPE_HIT, HIT_DAMAGE);
    ensure_damage_meter(*context.second_fighter).add_damage(TYPE_HIT, HIT_DAMAGE);

    tick_damage_meters(FIRST_ROUND_SECONDS);
    tick_damage_meters(SECOND_ROUND_SECONDS);

    const std::string expected = combat_time_text(FIRST_ROUND_SECONDS + SECOND_ROUND_SECONDS);
    const std::string first_report = get_damage_meter_report(*context.first_fighter);
    const std::string second_report = get_damage_meter_report(*context.second_fighter);
    EXPECT_NE(first_report.find(expected), std::string::npos) << first_report;
    EXPECT_NE(second_report.find(expected), std::string::npos) << second_report;
}

TEST(DamageMeters, CountsCombatTimeForAPlayerWhoHasNotDealtDamageYet)
{
    CombatListContext context;
    ASSERT_EQ(context.first_fighter->damage_details, nullptr);

    tick_damage_meters(FIRST_ROUND_SECONDS);
    ASSERT_NE(context.first_fighter->damage_details, nullptr) << "the first round gives the player a meter";
    ensure_damage_meter(*context.first_fighter).add_damage(TYPE_HIT, HIT_DAMAGE);
    tick_damage_meters(SECOND_ROUND_SECONDS);

    const std::string report = get_damage_meter_report(*context.first_fighter);
    const std::string expected = combat_time_text(FIRST_ROUND_SECONDS + SECOND_ROUND_SECONDS);
    EXPECT_NE(report.find(expected), std::string::npos) << report;
}

TEST(DamageMeters, GivesNoMeterToAFightingMobThatIsNotACharmedFollower)
{
    CombatListContext context;
    context.make_second_fighter_a_mob();

    tick_damage_meters(FIRST_ROUND_SECONDS);

    EXPECT_NE(context.first_fighter->damage_details, nullptr);
    EXPECT_EQ(context.second_fighter->damage_details, nullptr);
}

TEST(DamageMeters, CountsCombatTimeForACharmedFollower)
{
    CombatListContext context;
    context.make_second_fighter_a_mob();
    context.second_fighter->specials.affected_by |= AFF_CHARM;
    context.second_fighter->master = context.first_fighter;

    tick_damage_meters(FIRST_ROUND_SECONDS);
    ensure_damage_meter(*context.second_fighter).add_damage(TYPE_HIT, HIT_DAMAGE);

    const std::string report = get_damage_meter_report(*context.second_fighter);
    EXPECT_NE(report.find(combat_time_text(FIRST_ROUND_SECONDS)), std::string::npos) << report;
    context.second_fighter->master = nullptr;
}

TEST(DamageMeters, KeptByPlayersAndCharmedMobsWithAMaster)
{
    const owned_character player;
    const owned_character master;
    const owned_character mob(MOB_ISNPC);
    mob->specials2.act = MOB_ISNPC;
    EXPECT_TRUE(keeps_damage_meter(*player));
    EXPECT_FALSE(keeps_damage_meter(*mob));

    mob->specials.affected_by |= AFF_CHARM;
    EXPECT_FALSE(keeps_damage_meter(*mob)) << "charmed, but following nobody";
    mob->master = master;
    EXPECT_TRUE(keeps_damage_meter(*mob));
    mob->specials.affected_by &= ~AFF_CHARM;
    EXPECT_FALSE(keeps_damage_meter(*mob)) << "following, but no longer charmed";
    mob->master = nullptr;
}

TEST(DamageMeters, ResettingACharacterWithNoMeterLeavesItWithout)
{
    const owned_character character;

    reset_damage_meter(*character);

    EXPECT_EQ(character->damage_details, nullptr);
}

TEST(DamageMeters, ResettingEmptiesAMeter)
{
    const owned_character character;
    character->player.name = str_dup(PLAYER_NAME);
    ensure_damage_meter(*character).add_damage(TYPE_HIT, HIT_DAMAGE);

    reset_damage_meter(*character);

    ASSERT_NE(character->damage_details, nullptr);
    EXPECT_EQ(get_damage_meter_report(*character), no_damage_report(PLAYER_NAME));
}

TEST(DamageMeters, ReportsNoDamageForACharacterWithNoMeter)
{
    const owned_character character;
    character->player.name = str_dup(PLAYER_NAME);

    EXPECT_EQ(get_damage_meter_report(*character), no_damage_report(PLAYER_NAME));
    EXPECT_EQ(character->damage_details, nullptr) << "reporting does not create a meter";
}

TEST(DamageMeters, FreeingTheMeterLeavesTheCharacterWithout)
{
    const owned_character character;
    ensure_damage_meter(*character).add_damage(TYPE_HIT, HIT_DAMAGE);

    free_damage_meter(*character);

    EXPECT_EQ(character->damage_details, nullptr);
}

// Not portable standard C++, as in memory_release_tests.cpp: a free cannot be observed without
// changing the game, so this counts the allocator's bytes in use and is skipped where it cannot.
TEST(DamageMeters, FreeCharDeletesTheMeter)
{
    if (!allocated_bytes()) {
        GTEST_SKIP() << "the allocator cannot report the bytes in use";
    }
    // How many characters the test makes and frees while measuring.
    constexpr int ROUNDS = 128;
    // Characters made before measuring, so the allocator's cache of freed blocks is warm.
    constexpr int WARM_UP_ROUNDS = 32;
    // Growth allowed over all the rounds without a leak: a quarter of one meter per round.
    constexpr std::size_t ALLOWED_GROWTH = ROUNDS * sizeof(player_damage_details) / 4;

    const auto make_and_free_a_fighter = []() -> void {
        const owned_character fighter;
        ensure_damage_meter(*fighter).add_damage(TYPE_HIT, HIT_DAMAGE);
    };
    for (int warm_up = 0; warm_up < WARM_UP_ROUNDS; ++warm_up) {
        make_and_free_a_fighter();
    }
    const std::size_t before = *allocated_bytes();
    for (int measured = 0; measured < ROUNDS; ++measured) {
        make_and_free_a_fighter();
    }
    const std::size_t after = *allocated_bytes();

    // Less in use than before is no growth.
    std::size_t growth = 0;
    if (after > before) {
        growth = after - before;
    }
    EXPECT_LT(growth, ALLOWED_GROWTH) << "free_char keeps each character's meter";
}

namespace {
// One mob prototype in the tables, which the test reads into the world.
class ReadMobileDamageMeterTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        m_saved_mob_proto = mob_proto;
        m_saved_mob_index = mob_index;
        m_saved_top_of_mobt = top_of_mobt;
        m_saved_character_list = character_list;

        m_mob_proto[0].specials2.act = MOB_ISNPC;
        m_mob_proto[0].player.short_descr = m_prototype_short_description;
        m_mob_proto[0].nr = 0;
        char_ability_data& abilities = m_mob_proto[0].abilities;
        abilities.str = PROTOTYPE_ABILITY;
        abilities.lea = PROTOTYPE_ABILITY;
        abilities.intel = PROTOTYPE_ABILITY;
        abilities.wil = PROTOTYPE_ABILITY;
        abilities.dex = PROTOTYPE_ABILITY;
        abilities.con = PROTOTYPE_ABILITY;
        m_mob_index[0].virt = PROTOTYPE_VNUM;
        mob_proto = m_mob_proto;
        mob_index = m_mob_index;
        top_of_mobt = 0;
    }

    void TearDown() override
    {
        free_damage_meter(m_mob_proto[0]);
        mob_proto = m_saved_mob_proto;
        mob_index = m_saved_mob_index;
        top_of_mobt = m_saved_top_of_mobt;
        character_list = m_saved_character_list;
    }

    // Removes a mob read_mobile listed first in character_list, and frees it.
    static void release_mob(char_data* mob)
    {
        character_list = mob->next;
        free_char(mob);
    }

    // The virtual number of the test's only mob prototype.
    static constexpr int PROTOTYPE_VNUM = 6200;
    // Every ability of the prototype; read_mobile reports and replaces one at or below zero.
    static constexpr int PROTOTYPE_ABILITY = 12;

    // The mob prototype table the test installs as mob_proto.
    char_data m_mob_proto[1] {};
    // The mob index table the test installs as mob_index.
    index_data m_mob_index[1] {};
    // Storage for the prototype's short description.
    char m_prototype_short_description[16] = "a cave troll";

private:
    // The mob_proto in place before SetUp replaced it.
    char_data* m_saved_mob_proto = nullptr;
    // The mob_index in place before SetUp replaced it.
    index_data* m_saved_mob_index = nullptr;
    // The top_of_mobt in place before SetUp replaced it.
    int m_saved_top_of_mobt = 0;
    // The character_list in place before the test read any mob.
    char_data* m_saved_character_list = nullptr;
};
} // namespace

TEST_F(ReadMobileDamageMeterTest, AMobReadFromItsPrototypeHasNoMeterOfItsOwn)
{
    // Prototypes never fight; one is given a meter here only to show it is not shared.
    ensure_damage_meter(m_mob_proto[0]).add_damage(TYPE_HIT, HIT_DAMAGE);

    char_data* first_mob = read_mobile(0, REAL);
    ASSERT_NE(first_mob, nullptr);
    char_data* second_mob = read_mobile(0, REAL);
    ASSERT_NE(second_mob, nullptr);

    EXPECT_EQ(first_mob->damage_details, nullptr);
    EXPECT_EQ(second_mob->damage_details, nullptr);

    release_mob(second_mob);
    release_mob(first_mob);
}
