// Pins for the poison-death classification carveout: the pure decision
// functions that decide how a PC poison death is punished.
// The harness cannot drive a PC-victim death end-to-end (see the banner in
// fight_credit_tests.cpp), so these pins target the extracted functions
// directly with stack fixtures -- no rooms, no death pipeline.
#include "../handler.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include <gtest/gtest.h>

extern char_data* combat_list;

namespace {

// Saves/restores the global combat_list around an engagement pin, so one
// test's fighters never leak into another suite's walk. Per-suite copy,
// following fight_credit_tests.cpp's stated precedent.
struct CombatListGuard {
    char_data* previous; // combat_list found before the test; restored on scope exit
    CombatListGuard()
        : previous(combat_list)
    {
        combat_list = nullptr;
    }
    ~CombatListGuard() { combat_list = previous; }
};

void make_plain_mob(char_data& mob)
{
    mob.specials2.act = MOB_ISNPC;
}

void make_pet_mob(char_data& mob)
{
    mob.specials2.act = MOB_ISNPC | MOB_PET;
}

void make_orc_friend_mob(char_data& mob)
{
    mob.specials2.act = MOB_ISNPC | MOB_ORC_FRIEND;
}

} // namespace

TEST(IsRealMob, NullPlayersPetsAndOrcFriendsAreNotRealMobs)
{
    EXPECT_FALSE(is_real_mob(nullptr));

    char_data player { };
    player.player.level = 20;
    EXPECT_FALSE(is_real_mob(&player)) << "a PC is never a real mob";

    char_data pet { };
    make_pet_mob(pet);
    EXPECT_FALSE(is_real_mob(&pet)) << "a MOB_PET is player-combat context";

    char_data orc_friend { };
    make_orc_friend_mob(orc_friend);
    EXPECT_FALSE(is_real_mob(&orc_friend)) << "a MOB_ORC_FRIEND is player-combat context";
}

TEST(IsRealMob, PlainNpcIsARealMob)
{
    char_data mob { };
    make_plain_mob(mob);
    EXPECT_TRUE(is_real_mob(&mob));
}

TEST(ClassifyPcDeath, NonPoisonIsLegacyRegardlessOfEngagement)
{
    EXPECT_EQ(classify_pc_death(TYPE_HIT, true), death_punishment::legacy);
    EXPECT_EQ(classify_pc_death(TYPE_HIT, false), death_punishment::legacy);
    EXPECT_EQ(classify_pc_death(TYPE_UNDEFINED, true), death_punishment::legacy);
    EXPECT_EQ(classify_pc_death(TYPE_UNDEFINED, false), death_punishment::legacy);
    EXPECT_EQ(classify_pc_death(SPELL_BLAZE, true), death_punishment::legacy)
        << "room-affect damage ticks are outside the carveout";
}

TEST(ClassifyPcDeath, PoisonEngagedIsMobDeathUnengagedIsPlayerDeath)
{
    EXPECT_EQ(classify_pc_death(SPELL_POISON, true), death_punishment::mob_death);
    EXPECT_EQ(classify_pc_death(SPELL_POISON, false), death_punishment::player_death);
}
