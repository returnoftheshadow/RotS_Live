// Big Brother's engagement rules judged from a caster_snapshot, the form a room affect's tick
// uses once its caster has left the game. Where the attacker is still in the game, each pin
// also asks the live overload, which must agree.

#include "../big_brother.h"
#include "../caster_snapshot.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"

#include <gtest/gtest.h>

extern struct room_data world;
extern struct weather_data weather_info;

namespace {

// The abs_number the looting pin gives its victim, in a band no sibling suite uses: Big Brother
// tracks looting players by number.
constexpr int kLootingVictimNumber = MAX_CHARACTERS - 1401;

// A player of `level` and `race`, with the storage caster_snapshot::capture() reads.
struct TestPlayer {
    char_data character {}; // the player
    char_prof_data profs {}; // backs character.profs, which capture() reads
    char name[16] = "bb-player"; // backs character.player.name

    TestPlayer(int level, int race)
    {
        character.profs = &profs;
        character.player.name = name;
        character.player.level = level;
        character.player.race = race;
    }
};

// Boot creates the singleton, and so does gtest_main.cpp; create() keeps one function-local
// instance, so calling it again changes nothing.
game_rules::big_brother& big_brother_rules()
{
    game_rules::big_brother::create(weather_info, &world);
    return game_rules::big_brother::instance();
}

// Expects `attacker` judged from its snapshot to get `expected` against `victim` with
// `skill_id`, and the live overload to agree.
void expect_verdict(char_data& attacker, const char_data& victim, int skill_id, bool expected)
{
    const game_rules::big_brother& rules = big_brother_rules();
    const caster_snapshot snapshot = caster_snapshot::capture(attacker);
    EXPECT_EQ(rules.is_target_valid(snapshot, &victim, skill_id), expected) << "judged from the snapshot";
    EXPECT_EQ(rules.is_target_valid(&attacker, &victim, skill_id), expected) << "judged live";
}

} // namespace

// A character never protects itself from itself, even while held AFK.
TEST(BigBrother, ASnapshotAgainstItsOwnCharacterIsValid)
{
    game_rules::big_brother& rules = big_brother_rules();
    TestPlayer player(10, RACE_HUMAN);
    rules.on_character_afked(&player.character);

    expect_verdict(player.character, player.character, SPELL_BLAZE, true);

    rules.on_character_returned(&player.character);
}

TEST(BigBrother, ASnapshotNamingNobodyIsValid)
{
    TestPlayer victim(10, RACE_HUMAN);

    EXPECT_TRUE(big_brother_rules().is_target_valid(caster_snapshot::none(), &victim.character, SPELL_BLAZE));
}

TEST(BigBrother, AnNpcSnapshotIsValidAgainstAProtectedPlayer)
{
    TestPlayer mob(30, RACE_ORC);
    mob.character.specials2.act = MOB_ISNPC;
    TestPlayer victim(10, RACE_HUMAN);

    expect_verdict(mob.character, victim.character, SPELL_BLAZE, true);
}

// The snapshot cannot reach a charmed pet's master, so it judges the pet as a plain NPC: the
// live overload refuses the pet what its master may not attack, the snapshot does not.
TEST(BigBrother, ACharmedPetSnapshotIsJudgedAsAPlainNpc)
{
    const game_rules::big_brother& rules = big_brother_rules();
    TestPlayer master(30, RACE_HUMAN);
    TestPlayer pet(30, RACE_HUMAN);
    pet.character.specials2.act = MOB_ISNPC | MOB_PET;
    SET_BIT(pet.character.specials.affected_by, AFF_CHARM);
    pet.character.master = &master.character;
    TestPlayer victim(10, RACE_ORC);

    EXPECT_FALSE(rules.is_target_valid(&pet.character, &victim.character, SPELL_BLAZE))
        << "live, the pet is judged as its level-30 master";
    EXPECT_TRUE(rules.is_target_valid(caster_snapshot::capture(pet.character), &victim.character, SPELL_BLAZE))
        << "from the snapshot, the pet is an NPC";
}

// A player may not attack one with a third of its level or less, nor one with three times its
// level or more; the snapshot supplies the attacker's level.
TEST(BigBrother, TheLevelBandRefusesAVictimThreeTimesApart)
{
    TestPlayer veteran(30, RACE_HUMAN);
    TestPlayer lowbie(10, RACE_ORC);
    TestPlayer peer(11, RACE_ORC);

    expect_verdict(veteran.character, lowbie.character, SPELL_BLAZE, false);
    expect_verdict(veteran.character, peer.character, SPELL_BLAZE, true);
    expect_verdict(lowbie.character, veteran.character, SPELL_BLAZE, false);
}

// A victim inside the band is still refused while it is a god, AFK, looting or writing.
TEST(BigBrother, AGodAfkLootingOrWritingVictimIsRefused)
{
    game_rules::big_brother& rules = big_brother_rules();
    TestPlayer attacker(30, RACE_HUMAN);
    TestPlayer victim(25, RACE_ORC);
    victim.character.abs_number = kLootingVictimNumber;
    expect_verdict(attacker.character, victim.character, SPELL_BLAZE, true);

    {
        SCOPED_TRACE("god");
        victim.character.player.level = LEVEL_MINIMM;
        expect_verdict(attacker.character, victim.character, SPELL_BLAZE, false);
        victim.character.player.level = 25;
    }
    {
        SCOPED_TRACE("AFK");
        rules.on_character_afked(&victim.character);
        expect_verdict(attacker.character, victim.character, SPELL_BLAZE, false);
        rules.on_character_returned(&victim.character);
    }
    {
        SCOPED_TRACE("looting");
        // A player's death with a non-empty corpse marks the player as looting until the corpse
        // decays.
        obj_data item {};
        obj_data corpse {};
        corpse.contains = &item;
        rules.on_character_died(&victim.character, nullptr, &corpse);
        expect_verdict(attacker.character, victim.character, SPELL_BLAZE, false);
        rules.on_corpse_decayed(&corpse);
    }
    {
        SCOPED_TRACE("writing");
        SET_BIT(victim.character.specials2.act, PLR_WRITING);
        expect_verdict(attacker.character, victim.character, SPELL_BLAZE, false);
        REMOVE_BIT(victim.character.specials2.act, PLR_WRITING);
    }

    expect_verdict(attacker.character, victim.character, SPELL_BLAZE, true);
}

// A pet is protected as its master is, and a caster's own pet is its own to burn.
TEST(BigBrother, APetVictimIsJudgedAsItsMaster)
{
    TestPlayer attacker(30, RACE_HUMAN);
    TestPlayer lowbie(10, RACE_ORC);
    TestPlayer peer(25, RACE_ORC);
    TestPlayer pet(5, RACE_ORC);
    pet.character.specials2.act = MOB_ISNPC | MOB_PET;

    pet.character.master = &lowbie.character;
    expect_verdict(attacker.character, pet.character, SPELL_BLAZE, false);

    pet.character.master = &peer.character;
    expect_verdict(attacker.character, pet.character, SPELL_BLAZE, true);

    pet.character.master = &attacker.character;
    expect_verdict(attacker.character, pet.character, SPELL_BLAZE, true);
}

// On a protected player, a harmful spell is refused, a spell that can help is allowed on the
// caster's own side of the war only, and a spell in neither list is allowed.
TEST(BigBrother, AProtectedPlayerTakesOnlyHelpFromItsOwnSide)
{
    TestPlayer attacker(30, RACE_HUMAN);
    TestPlayer same_side(10, RACE_HUMAN);
    TestPlayer other_side(10, RACE_ORC);

    expect_verdict(attacker.character, same_side.character, SPELL_CURING, true);
    expect_verdict(attacker.character, other_side.character, SPELL_CURING, false);
    expect_verdict(attacker.character, same_side.character, SPELL_BLAZE, false);
    expect_verdict(attacker.character, other_side.character, SPELL_ARMOR, true);
}
