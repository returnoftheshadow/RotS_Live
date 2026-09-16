#include "gtest/gtest.h"

#include "spells.h"
#include "structs.h"
#include "utils.h"

extern struct skill_data skills[];

int cast_resist_magnitude(int caster_level);

TEST(ResistanceIdentity, DamagingSpellsCarryTheirElement)
{
    EXPECT_EQ(skills[SPELL_FIREBOLT].resist, RESIST_FIRE);
    EXPECT_EQ(skills[SPELL_FIREBALL].resist, RESIST_FIRE);
    EXPECT_EQ(skills[SPELL_BLAZE].resist, RESIST_FIRE);
    EXPECT_EQ(skills[SPELL_CHILL_RAY].resist, RESIST_COLD);
    EXPECT_EQ(skills[SPELL_CONE_OF_COLD].resist, RESIST_COLD);
    EXPECT_EQ(skills[SPELL_LIGHTNING_BOLT].resist, RESIST_LGHT);
    EXPECT_EQ(skills[SPELL_LIGHTNING_STRIKE].resist, RESIST_LGHT);
    EXPECT_EQ(skills[SPELL_DARK_BOLT].resist, RESIST_DARK);
    EXPECT_EQ(skills[SPELL_SEARING_DARKNESS].resist, RESIST_DARK);
    EXPECT_EQ(skills[SPELL_SPEAR_OF_DARKNESS].resist, RESIST_DARK);
    EXPECT_EQ(skills[SPELL_BLACK_ARROW].resist, RESIST_DARK);
}

TEST(ResistanceIdentity, DarkIsNotTheSpecIndex)
{
    // The whole point of the separate field: PLRSPEC_DARK is 16, the dark resistance bit is 12.
    EXPECT_EQ(RESIST_DARK, 12);
    EXPECT_EQ(PLRSPEC_DARK, 16);
    EXPECT_NE(static_cast<int>(skills[SPELL_DARK_BOLT].resist), static_cast<int>(skills[SPELL_DARK_BOLT].skill_spec));
}

TEST(ResistTypeForAttack, MapsSpellsWeaponsAndUnknowns)
{
    EXPECT_EQ(resist_type_for_attack(SPELL_FIREBOLT), RESIST_FIRE);
    EXPECT_EQ(resist_type_for_attack(SPELL_MAGIC_MISSILE), RESIST_NONE);

    EXPECT_EQ(resist_type_for_attack(TYPE_HIT), RESIST_PHYS);
    EXPECT_EQ(resist_type_for_attack(TYPE_CRUSH), RESIST_PHYS);
    EXPECT_EQ(resist_type_for_attack(SKILL_ARCHERY), RESIST_PHYS);

    EXPECT_EQ(resist_type_for_attack(TYPE_SUFFERING), RESIST_NONE);
    EXPECT_EQ(resist_type_for_attack(MAX_SKILLS + 10), RESIST_NONE);
    EXPECT_EQ(resist_type_for_attack(-1), RESIST_NONE);
}

TEST(ResistSpell, CastMagnitudeIsLevelPlusTenCappedAtForty)
{
    EXPECT_EQ(cast_resist_magnitude(1), 11);
    EXPECT_EQ(cast_resist_magnitude(29), 39);
    EXPECT_EQ(cast_resist_magnitude(30), 40);
    EXPECT_EQ(cast_resist_magnitude(60), 40) << "a cast resist is capped at 40%";
}

TEST(ResistSpell, SkillRowsSitAtTheirSpellNumbers)
{
    // The row before the block must not have moved.
    EXPECT_STREQ(skills[SPELL_MASS_INSIGHT].name, "mass insight");

    EXPECT_STREQ(skills[SPELL_RESIST_FIRE].name, "resist fire");
    EXPECT_STREQ(skills[SPELL_RESIST_COLD].name, "resist cold");
    EXPECT_STREQ(skills[SPELL_RESIST_LIGHT].name, "resist lightning");
    EXPECT_STREQ(skills[SPELL_RESIST_ILLUSION].name, "resist illusion");
    EXPECT_STREQ(skills[SPELL_RESIST_PHYSICAL].name, "resist physical");
    EXPECT_STREQ(skills[SPELL_RESIST_DARK].name, "resist dark");

    // Every field after the function pointer lands where it should, i.e. the rows are
    // the same width as the rest of the table.
    for (int spell = SPELL_RESIST_FIRE; spell <= SPELL_RESIST_DARK; ++spell) {
        EXPECT_NE(skills[spell].spell_pointer, nullptr) << "spell " << spell;
        EXPECT_EQ(skills[spell].type, PROF_CLERIC) << "spell " << spell;
        EXPECT_EQ(skills[spell].min_usesmana, 5) << "spell " << spell;
        EXPECT_EQ(skills[spell].beats, 21) << "spell " << spell;
        EXPECT_EQ(skills[spell].targets, 32) << "spell " << spell;
        EXPECT_EQ(skills[spell].learn_diff, 10) << "spell " << spell;
        EXPECT_EQ(skills[spell].learn_type, 1) << "spell " << spell;
        EXPECT_EQ(skills[spell].is_fast, 0) << "spell " << spell;
        EXPECT_EQ(skills[spell].skill_spec, PLRSPEC_PROT) << "spell " << spell;
        EXPECT_EQ(skills[spell].resist, RESIST_NONE) << "spell " << spell;
    }
}
