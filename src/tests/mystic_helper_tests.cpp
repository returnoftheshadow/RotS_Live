// The victim-side formulas mystic.cpp's casts share with the room-affect ticks, and the
// spell_fear cast pins that hold it to the same snapshot rule.
#include "../caster_snapshot.h"
#include "../handler.h"
#include "../poison.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
#include "test_character_support.h"
#include "test_random_utils.h"
#include <gtest/gtest.h>

namespace {

// A player mystic whose snapshot the helpers read; wil is a multiple of 5 so
// get_mystic_caster_level() draws nothing from the RNG.
struct MysticFixture {
    char_data ch {}; // the mystic; captured into a caster_snapshot by each test
    char_prof_data profs {}; // backs ch.profs; holds the cleric level and specialization
    char name[16] = "test_mystic"; // backs ch.player.name for the fixture's lifetime

    explicit MysticFixture(game_types::player_specs specialization)
    {
        ch.profs = &profs;
        ch.player.name = name;
        ch.player.race = RACE_HUMAN;
        ch.player.level = 30;
        profs.prof_level[PROF_CLERIC] = 20;
        profs.specialization = static_cast<int>(specialization);
        ch.tmpabilities.intel = 20;
        ch.tmpabilities.wil = 25;
    }
};

} // namespace

TEST(MysticHelpers, PoisonVictimAffectLastsTheMysticLevelPlusOneAndDrainsStrength)
{
    MysticFixture mystic(game_types::PS_None);
    const caster_snapshot who = caster_snapshot::capture(mystic.ch);

    const affected_type poison = poison_victim_affect(who);

    EXPECT_EQ(poison.type, SPELL_POISON);
    EXPECT_EQ(poison.duration, get_mystic_caster_level(who) + 1);
    EXPECT_EQ(poison.modifier, -2);
    EXPECT_EQ(poison.location, APPLY_STR);
    EXPECT_EQ(poison.bitvector, AFF_POISON);
}

TEST(MysticHelpers, IllusionCasterLevelAddsSixForAnIllusionist)
{
    MysticFixture plain(game_types::PS_None);
    MysticFixture illusionist(game_types::PS_Illusion);
    const caster_snapshot plain_snapshot = caster_snapshot::capture(plain.ch);
    const caster_snapshot illusion_snapshot = caster_snapshot::capture(illusionist.ch);

    EXPECT_EQ(illusion_caster_level(plain_snapshot), get_mystic_caster_level(plain_snapshot));
    EXPECT_EQ(illusion_caster_level(illusion_snapshot), get_mystic_caster_level(illusion_snapshot) + 6);
}

TEST(MysticHelpers, HazeVictimAffectCarriesTheLevelAndDuration)
{
    const affected_type haze = haze_victim_affect(17, -1);

    EXPECT_EQ(haze.type, SPELL_HAZE);
    EXPECT_EQ(haze.duration, -1) << "an object's haze is permanent";
    EXPECT_EQ(haze.modifier, 17);
    EXPECT_EQ(haze.location, APPLY_NONE);
    EXPECT_EQ(haze.bitvector, AFF_HAZE);
}

TEST(MysticHelpers, PoisonVictimAffectAtLevelLastsThatLevelPlusOne)
{
    const affected_type poison = poison_victim_affect_at_level(34);

    EXPECT_EQ(poison.type, SPELL_POISON);
    EXPECT_EQ(poison.duration, 35);
    EXPECT_EQ(poison.modifier, -2);
    EXPECT_EQ(poison.location, APPLY_STR);
    EXPECT_EQ(poison.bitvector, AFF_POISON);
}

namespace {

// Casts fear from `caster` at `target` with both saving throws failed: saves_mystic() rolls
// number(0, 100) against perception 0 (a save only on a zero roll) and saves_leadership()
// rolls it once more. get_mystic_caster_level() draws nothing for wil 25.
void cast_fear_that_lands(MysticFixture& caster, MysticFixture& target, const caster_snapshot& caster_at_cast)
{
    // fear's act() calls look up only the caster's room, and skip the room audience for NOWHERE
    caster.ch.in_room = NOWHERE;
    clear_test_random_values();
    push_test_random_value(0.99); // saves_mystic(): number(0, 100) = 99, no save
    push_test_random_value(0.99); // saves_leadership()'s own saves_mystic() call
    spell_fear(&caster.ch, nullptr, SPELL_TYPE_SPELL, &target.ch, nullptr, 0, 0, caster_at_cast);
    clear_test_random_values();
}

// A mob target: spell_fear refuses only good-on-good PLAYER fear, so a mob target always
// reaches the saving throws.
void prepare_fear_target(MysticFixture& target)
{
    target.ch.in_room = NOWHERE; // keeps the target out of any room
    target.ch.specials2.act = MOB_ISNPC;
    target.ch.specials2.perception = 0;
}

} // namespace

// spell_fear reads the illusion bonus from the cast-time snapshot, as haze does, so a
// specialization the caster dropped after the snapshot still counts.
TEST(MysticHelpers, FearCastAppliesTheIllusionBonusFromTheSnapshot)
{
    MysticFixture illusionist(game_types::PS_Illusion);
    MysticFixture target(game_types::PS_None);
    prepare_fear_target(target);
    test_support::ScopedAffectCleanup target_affects(target.ch);

    const caster_snapshot caster_at_cast = caster_snapshot::capture(illusionist.ch);
    illusionist.profs.specialization = static_cast<int>(game_types::PS_None); // dropped after the snapshot

    cast_fear_that_lands(illusionist, target, caster_at_cast);

    const affected_type* fear = affected_by_spell(&target.ch, SPELL_FEAR);
    ASSERT_NE(fear, nullptr) << "with both saves failed, fear must land";
    EXPECT_EQ(fear->duration, get_mystic_caster_level(caster_at_cast) + 6)
        << "the illusion bonus comes from the snapshot, not the live caster";
    EXPECT_EQ(fear->modifier, get_mystic_caster_level(caster_at_cast) + 6 + 10);
}

// ...and a specialization gained after the snapshot does not count.
TEST(MysticHelpers, FearCastIgnoresAnIllusionBonusGainedAfterTheSnapshot)
{
    MysticFixture caster(game_types::PS_None);
    MysticFixture target(game_types::PS_None);
    prepare_fear_target(target);
    test_support::ScopedAffectCleanup target_affects(target.ch);

    const caster_snapshot caster_at_cast = caster_snapshot::capture(caster.ch);
    caster.profs.specialization = static_cast<int>(game_types::PS_Illusion); // gained after the snapshot

    cast_fear_that_lands(caster, target, caster_at_cast);

    const affected_type* fear = affected_by_spell(&target.ch, SPELL_FEAR);
    ASSERT_NE(fear, nullptr) << "with both saves failed, fear must land";
    EXPECT_EQ(fear->duration, get_mystic_caster_level(caster_at_cast))
        << "no bonus: the snapshot was taken before the caster became an illusionist";
}
