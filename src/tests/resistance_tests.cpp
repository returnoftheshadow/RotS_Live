#include "gtest/gtest.h"

#include "spells.h"
#include "structs.h"
#include "utils.h"

extern struct skill_data skills[];
extern room_data world;
extern int top_of_world;

int cast_resist_magnitude(int caster_level);
int apply_resistance(int dam, int magnitude);
int resist_magnitude_for(char_data* victim, int resist_type);
int damage(char_data* attacker, char_data* victim, int dam, int attacktype, int hit_location);

extern char* resistance_name[];
extern char* vulnerability_name[];

namespace {

/* The smallest world and pair of combatants damage() will accept, mirroring damage_tests.cpp. */
struct ResistDamageContext {
    static constexpr int room_number = 1;

    char_data attacker {};
    char_data victim {};
    affected_type resist {};
    char attacker_name[16] = "resist_att";
    char victim_name[16] = "resist_vic";
    char_data* original_people = nullptr;

    ResistDamageContext()
    {
        if (!room_data::BASE_WORLD) {
            world.create_bulk(room_number + 2);
            top_of_world = room_number + 1;
        } else if (top_of_world < room_number) {
            top_of_world = room_number;
        }

        original_people = world[room_number].people;

        attacker.specials2.act = MOB_ISNPC;
        victim.specials2.act = MOB_ISNPC;
        attacker.player.short_descr = attacker_name;
        victim.player.short_descr = victim_name;
        attacker.player.race = RACE_HUMAN;
        victim.player.race = RACE_HUMAN;
        attacker.player.level = 20;
        victim.player.level = 20;
        attacker.tmpabilities.con = 20;
        victim.tmpabilities.con = 20;
        attacker.abilities.hit = 500;
        victim.abilities.hit = 500;
        attacker.tmpabilities.hit = 500;
        victim.tmpabilities.hit = 500;
        attacker.specials.position = POSITION_FIGHTING;
        victim.specials.position = POSITION_FIGHTING;
        attacker.specials.fighting = &victim;
        victim.specials.fighting = &attacker;
        attacker.in_room = room_number;
        victim.in_room = room_number;
        attacker.next_in_room = &victim;
        victim.next_in_room = nullptr;
        world[room_number].people = &attacker;
    }

    ~ResistDamageContext()
    {
        world[room_number].people = original_people;
        attacker.next_in_room = nullptr;
        victim.next_in_room = nullptr;
        attacker.specials.fighting = nullptr;
        victim.specials.fighting = nullptr;
        attacker.in_room = NOWHERE;
        victim.in_room = NOWHERE;
    }

    /* A resist affect of the shape the resist spells and spell_protection write. */
    void resist_element(int resist_type, int magnitude)
    {
        resist = {};
        resist.type = SPELL_RESIST_FIRE;
        resist.duration = 10;
        resist.location = APPLY_RESIST;
        resist.modifier = resist_type;
        resist.effect_modifier = magnitude;
        resist.next = victim.affected;
        victim.affected = &resist;
        victim.specials.resistance |= (1 << resist_type);
    }
};

} // namespace

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

TEST(ApplyResistance, ReducesByThePercentageAndRoundsHalfAway)
{
    EXPECT_EQ(apply_resistance(100, 40), 60);
    EXPECT_EQ(apply_resistance(100, 30), 70);
    EXPECT_EQ(apply_resistance(10, 33), 7) << "3.3 rounds to 3";
    EXPECT_EQ(apply_resistance(11, 50), 5) << "5.5 rounds to 6, leaving 5";
    EXPECT_EQ(apply_resistance(100, 0), 100) << "no magnitude means no reduction here";
    EXPECT_EQ(apply_resistance(1, 100), 0);
}

TEST(ApplyResistance, LeavesDamageAloneForNonsenseMagnitudes)
{
    EXPECT_EQ(apply_resistance(50, -10), 50);
    EXPECT_EQ(apply_resistance(0, 40), 0);
}

TEST(ResistMagnitudeFor, MatchesOnTheElementAndNotMerelyOnBeingAResistAffect)
{
    char_data victim {};
    affected_type fire {};
    affected_type dark {};

    fire.location = APPLY_RESIST;
    fire.modifier = RESIST_FIRE;
    fire.effect_modifier = 30;
    fire.next = &dark;

    dark.location = APPLY_RESIST;
    dark.modifier = RESIST_DARK;
    dark.effect_modifier = 25;
    dark.next = nullptr;

    victim.affected = &fire;

    EXPECT_EQ(resist_magnitude_for(&victim, RESIST_FIRE), 30);
    EXPECT_EQ(resist_magnitude_for(&victim, RESIST_DARK), 25);
    EXPECT_EQ(resist_magnitude_for(&victim, RESIST_COLD), 0)
        << "a fire resistance must not answer for cold";
    EXPECT_EQ(resist_magnitude_for(&victim, RESIST_PHYS), 0);
}

TEST(ResistMagnitudeFor, IgnoresAffectsAtOtherLocationsAndEmptyLists)
{
    char_data victim {};
    affected_type strength {};

    strength.location = APPLY_STR;
    strength.modifier = RESIST_FIRE;
    strength.effect_modifier = 30;
    strength.next = nullptr;

    victim.affected = &strength;
    EXPECT_EQ(resist_magnitude_for(&victim, RESIST_FIRE), 0);

    victim.affected = nullptr;
    EXPECT_EQ(resist_magnitude_for(&victim, RESIST_FIRE), 0);
}

TEST(ResistMagnitudeFor, StopsOnACircularAffectList)
{
    // The walk is bounded by MAX_AFFECT so a corrupt list cannot hang the damage path.
    char_data victim {};
    affected_type loop {};

    loop.location = APPLY_STR;
    loop.modifier = 0;
    loop.effect_modifier = 0;
    loop.next = &loop;

    victim.affected = &loop;
    EXPECT_EQ(resist_magnitude_for(&victim, RESIST_FIRE), 0);
}

TEST(ResistanceDamage, AFireResistanceReducesFireDamageByItsMagnitude)
{
    ResistDamageContext context;
    context.resist_element(RESIST_FIRE, 30);

    damage(&context.attacker, &context.victim, 50, SPELL_FIREBOLT, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 465)
        << "50 fire damage against a 30% fire resistance should land as 35.";
}

TEST(ResistanceDamage, AFireResistanceDoesNothingAgainstColdDamage)
{
    // The bug this change exists to fix: the resistance used to fire for any element.
    ResistDamageContext context;
    context.resist_element(RESIST_FIRE, 30);

    damage(&context.attacker, &context.victim, 50, SPELL_CHILL_RAY, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 450)
        << "50 cold damage against a fire resistance should land unreduced.";
}

TEST(ResistanceDamage, AFlagOnlyResistanceStillTakesTheFlatOneThird)
{
    // A mob record or an item bit sets the resistance with no affect behind it, so there is
    // no magnitude to read and the original flat rule has to survive.
    ResistDamageContext context;
    context.victim.specials.resistance = (1 << RESIST_FIRE);

    damage(&context.attacker, &context.victim, 30, SPELL_FIREBOLT, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 480)
        << "30 fire damage against a flag-only fire resistance should land as 20.";
}

TEST(ResistanceDamage, AVulnerabilityStillAddsAHalf)
{
    ResistDamageContext context;
    context.victim.specials.vulnerability = (1 << RESIST_FIRE);

    damage(&context.attacker, &context.victim, 30, SPELL_FIREBOLT, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 455)
        << "30 fire damage against a fire vulnerability should land as 45.";
}

TEST(ResistMagnitudeFor, TheLargestMagnitudeWinsRegardlessOfListOrder)
{
    // A cast protection and a resist spell are different affect types that both write
    // APPLY_RESIST for the same element, so they can coexist. affect_to_char prepends, so
    // returning the first match would hand the win to whichever was applied most recently.
    char_data victim {};
    affected_type strong {};
    affected_type weak {};

    strong.type = SPELL_PROTECTION;
    strong.location = APPLY_RESIST;
    strong.modifier = RESIST_FIRE;
    strong.effect_modifier = 40;

    weak.type = SPELL_RESIST_FIRE;
    weak.location = APPLY_RESIST;
    weak.modifier = RESIST_FIRE;
    weak.effect_modifier = 10;

    strong.next = &weak;
    weak.next = nullptr;
    victim.affected = &strong;
    EXPECT_EQ(resist_magnitude_for(&victim, RESIST_FIRE), 40) << "strongest first";

    weak.next = &strong;
    strong.next = nullptr;
    victim.affected = &weak;
    EXPECT_EQ(resist_magnitude_for(&victim, RESIST_FIRE), 40) << "weakest first";

    // The loser's element is still answered on its own terms.
    weak.modifier = RESIST_COLD;
    EXPECT_EQ(resist_magnitude_for(&victim, RESIST_COLD), 10);
    EXPECT_EQ(resist_magnitude_for(&victim, RESIST_FIRE), 40);
}

TEST(RemovePattern, StripsTheVulnerabilityPrefix)
{
    char out[64];
    char pattern[] = "V-";
    char input[] = "V-FIRE";
    remove_pattern(input, out, pattern);
    EXPECT_STREQ(out, "FIRE");

    char untouched[] = "FIRE";
    remove_pattern(untouched, out, pattern);
    EXPECT_STREQ(out, "FIRE");
}

TEST(SprintbitResistances, UsesTheFlatDefaultWhenNoAffectBacksTheBit)
{
    // Mirrors a mob record or a flag-only APPLY_RESIST item: the bit is set but no affect exists.
    char_data victim {};
    char result[512];

    sprintbit_resistances(&victim, (1 << RESIST_FIRE), resistance_name, result, 33);
    EXPECT_STREQ(result, "   fire (33%)\r\n");
}

TEST(SprintbitResistances, ShowsAWeakAffectRatherThanTheDefault)
{
    // A real 11% resist-fire affect must not be hidden behind the (larger) 33% default.
    char_data victim {};
    affected_type weak {};
    weak.location = APPLY_RESIST;
    weak.modifier = RESIST_FIRE;
    weak.effect_modifier = 11;
    weak.next = nullptr;
    victim.affected = &weak;

    char result[512];
    sprintbit_resistances(&victim, (1 << RESIST_FIRE), resistance_name, result, 33);
    EXPECT_STREQ(result, "   fire (11%)\r\n");
}

TEST(SprintbitResistances, TheLargestOfTwoCoexistingAffectsWins)
{
    // A cast protection and a cast resist spell can coexist on the same element; the display
    // must agree with the damage path (resist_magnitude_for) and show the larger one.
    char_data victim {};
    affected_type strong {};
    affected_type weak {};

    strong.location = APPLY_RESIST;
    strong.modifier = RESIST_FIRE;
    strong.effect_modifier = 40;
    strong.next = &weak;

    weak.location = APPLY_RESIST;
    weak.modifier = RESIST_FIRE;
    weak.effect_modifier = 10;
    weak.next = nullptr;

    victim.affected = &strong;

    char result[512];
    sprintbit_resistances(&victim, (1 << RESIST_FIRE), resistance_name, result, 33);
    EXPECT_STREQ(result, "   fire (40%)\r\n");
}

TEST(SprintbitResistances, SkipsBlankSlotsAndStopsAtTheSentinel)
{
    // resistance_name[] has empty-string slots at indices 13-15 before the "\n" terminator;
    // a set bit there must not print a blank line, and the walk must not read past "\n".
    char_data victim {};
    char result[512];

    sprintbit_resistances(&victim, (1 << 13) | (1 << 14) | (1 << 15), resistance_name, result, 33);
    EXPECT_STREQ(result, "");
}

TEST(SprintbitResistances, RendersVulnerabilitiesWithoutTheVPrefix)
{
    char_data victim {};
    char result[512];

    sprintbit_resistances(&victim, (1 << RESIST_FIRE), vulnerability_name, result, 50);
    EXPECT_STREQ(result, "   fire (50%)\r\n");
}
