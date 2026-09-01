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

TEST(FindEngagedRealMob, EngagedOpponentRealMobIsFound)
{
    CombatListGuard combat_guard;
    char_data victim { };
    char_data mob { };
    make_plain_mob(mob);

    EXPECT_EQ(find_engaged_real_mob(&victim, &mob), &mob)
        << "the victim's own target engages even with an empty combat_list";
}

TEST(FindEngagedRealMob, EngagedOpponentPetOrPlayerDoesNotEngage)
{
    CombatListGuard combat_guard;
    char_data victim { };

    char_data pet { };
    make_pet_mob(pet);
    EXPECT_EQ(find_engaged_real_mob(&victim, &pet), nullptr);

    char_data player { };
    player.player.level = 20;
    EXPECT_EQ(find_engaged_real_mob(&victim, &player), nullptr);

    EXPECT_EQ(find_engaged_real_mob(&victim, nullptr), nullptr);
}

TEST(FindEngagedRealMob, CombatListMobFightingTheVictimEngagesWithoutBeingTargeted)
{
    CombatListGuard combat_guard;
    char_data victim { };
    char_data mob { };
    make_plain_mob(mob);
    mob.specials.fighting = &victim;
    combat_list = &mob;
    mob.next_fighting = nullptr;

    EXPECT_EQ(find_engaged_real_mob(&victim, nullptr), &mob)
        << "a mob beating on a victim who targets nobody still engages (the fled/stunned case)";
}

TEST(FindEngagedRealMob, CombatListWalkSkipsPetsOrcFriendsPlayersAndMobsFightingOthers)
{
    CombatListGuard combat_guard;
    char_data victim { };
    char_data bystander { };

    char_data pet { };
    make_pet_mob(pet);
    pet.specials.fighting = &victim;

    char_data orc_friend { };
    make_orc_friend_mob(orc_friend);
    orc_friend.specials.fighting = &victim;

    char_data player { };
    player.player.level = 20;
    player.specials.fighting = &victim;

    char_data distracted_mob { };
    make_plain_mob(distracted_mob);
    distracted_mob.specials.fighting = &bystander;

    combat_list = &pet;
    pet.next_fighting = &orc_friend;
    orc_friend.next_fighting = &player;
    player.next_fighting = &distracted_mob;
    distracted_mob.next_fighting = nullptr;

    EXPECT_EQ(find_engaged_real_mob(&victim, nullptr), nullptr);
}

TEST(FindEngagedRealMob, EngagedOpponentIsPreferredOverCombatListMob)
{
    CombatListGuard combat_guard;
    char_data victim { };
    char_data targeted_mob { };
    make_plain_mob(targeted_mob);
    char_data list_mob { };
    make_plain_mob(list_mob);
    list_mob.specials.fighting = &victim;
    combat_list = &list_mob;
    list_mob.next_fighting = nullptr;

    EXPECT_EQ(find_engaged_real_mob(&victim, &targeted_mob), &targeted_mob)
        << "the victim's own target names the EXPLOIT_MOBDEATH mob deterministically";
}

TEST(DeathTakesFullMobXpLoss, LegacyDerivesFromKiller)
{
    char_data mob { };
    make_plain_mob(mob);
    char_data pet { };
    make_pet_mob(pet);
    char_data orc_friend { };
    make_orc_friend_mob(orc_friend);
    char_data player { };
    player.player.level = 20;

    EXPECT_TRUE(death_takes_full_mob_xp_loss(&mob, death_punishment::legacy));
    EXPECT_FALSE(death_takes_full_mob_xp_loss(&pet, death_punishment::legacy));
    EXPECT_FALSE(death_takes_full_mob_xp_loss(&orc_friend, death_punishment::legacy));
    EXPECT_FALSE(death_takes_full_mob_xp_loss(&player, death_punishment::legacy));
    EXPECT_FALSE(death_takes_full_mob_xp_loss(nullptr, death_punishment::legacy))
        << "byte-compatible with die()'s removed IS_NPC(killer) branch, killer == null included";
}

TEST(DeathTakesFullMobXpLoss, MobDeathAlwaysLosesPlayerDeathNeverLoses)
{
    char_data mob { };
    make_plain_mob(mob);
    char_data player { };
    player.player.level = 20;

    EXPECT_TRUE(death_takes_full_mob_xp_loss(nullptr, death_punishment::mob_death));
    EXPECT_TRUE(death_takes_full_mob_xp_loss(&player, death_punishment::mob_death));
    EXPECT_TRUE(death_takes_full_mob_xp_loss(&mob, death_punishment::mob_death));

    EXPECT_FALSE(death_takes_full_mob_xp_loss(nullptr, death_punishment::player_death));
    EXPECT_FALSE(death_takes_full_mob_xp_loss(&player, death_punishment::player_death));
    EXPECT_FALSE(death_takes_full_mob_xp_loss(&mob, death_punishment::player_death))
        << "an unengaged mob poisoner takes the legacy small loss only";
}

TEST(DeathCountsAsPlayerKill, LegacyDerivesFromKiller)
{
    char_data mob { };
    make_plain_mob(mob);
    char_data player { };
    player.player.level = 20;

    EXPECT_FALSE(death_counts_as_player_kill(nullptr, death_punishment::legacy))
        << "raw_kill()'s removed expression: null killer is harsh, and the old"
           " attack_type == SPELL_POISON short-circuit stays gone from legacy";
    EXPECT_FALSE(death_counts_as_player_kill(&mob, death_punishment::legacy));
    EXPECT_TRUE(death_counts_as_player_kill(&player, death_punishment::legacy));
}

TEST(DeathCountsAsPlayerKill, MobDeathIsHarshAndPlayerDeathIsGentleRegardlessOfKiller)
{
    char_data mob { };
    make_plain_mob(mob);
    char_data player { };
    player.player.level = 20;

    EXPECT_FALSE(death_counts_as_player_kill(&player, death_punishment::mob_death))
        << "player poison while engaged with a mob is still punished as a mob death";
    EXPECT_FALSE(death_counts_as_player_kill(nullptr, death_punishment::mob_death));

    EXPECT_TRUE(death_counts_as_player_kill(&mob, death_punishment::player_death))
        << "an unengaged mob poisoner still yields the gentle player-kill penalty";
    EXPECT_TRUE(death_counts_as_player_kill(nullptr, death_punishment::player_death));
}

TEST(MobdeathRecordMob, LegacyNamesOnlyARealMobKiller)
{
    char_data mob { };
    make_plain_mob(mob);
    char_data pet { };
    make_pet_mob(pet);
    char_data player { };
    player.player.level = 20;
    char_data engaged_mob { };
    make_plain_mob(engaged_mob);

    EXPECT_EQ(mobdeath_record_mob(&mob, &engaged_mob, death_punishment::legacy), &mob);
    EXPECT_EQ(mobdeath_record_mob(&pet, &engaged_mob, death_punishment::legacy), nullptr);
    EXPECT_EQ(mobdeath_record_mob(&player, &engaged_mob, death_punishment::legacy), nullptr);
    EXPECT_EQ(mobdeath_record_mob(nullptr, &engaged_mob, death_punishment::legacy), nullptr)
        << "legacy never names the engaged mob -- byte-compatible with the removed guard";
}

TEST(MobdeathRecordMob, MobDeathNamesRealMobKillerElseTheEngagedMob)
{
    char_data mob { };
    make_plain_mob(mob);
    char_data player { };
    player.player.level = 20;
    char_data engaged_mob { };
    make_plain_mob(engaged_mob);

    EXPECT_EQ(mobdeath_record_mob(&mob, &engaged_mob, death_punishment::mob_death), &mob);
    EXPECT_EQ(mobdeath_record_mob(&player, &engaged_mob, death_punishment::mob_death), &engaged_mob);
    EXPECT_EQ(mobdeath_record_mob(nullptr, &engaged_mob, death_punishment::mob_death), &engaged_mob);
}

TEST(MobdeathRecordMob, PlayerDeathSuppressesTheRecordEvenForAMobKiller)
{
    char_data mob { };
    make_plain_mob(mob);

    EXPECT_EQ(mobdeath_record_mob(&mob, nullptr, death_punishment::player_death), nullptr)
        << "an unengaged mob-poison death is not recorded as a mob death";
}
