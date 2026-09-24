// The victim-side formulas mystic.cpp's casts share with the room-affect ticks.
#include "../caster_snapshot.h"
#include "../spells.h"
#include "../structs.h"
#include "../utils.h"
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

TEST(MysticHelpers, HazeCasterLevelAddsSixForAnIllusionist)
{
    MysticFixture plain(game_types::PS_None);
    MysticFixture illusionist(game_types::PS_Illusion);
    const caster_snapshot plain_snapshot = caster_snapshot::capture(plain.ch);
    const caster_snapshot illusion_snapshot = caster_snapshot::capture(illusionist.ch);

    EXPECT_EQ(haze_caster_level(plain_snapshot), get_mystic_caster_level(plain_snapshot));
    EXPECT_EQ(haze_caster_level(illusion_snapshot), get_mystic_caster_level(illusion_snapshot) + 6);
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
