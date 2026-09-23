#include "gtest/gtest.h"

#include <map>

#include "char_utils.h"
#include "db.h"
#include "handler.h"
#include "spells.h"
#include "structs.h"
#include "utils.h"

extern struct skill_data skills[];
extern room_data world;
extern int top_of_world;
extern struct char_data* mob_proto;
extern int eff_mod;

int cast_resist_magnitude(int caster_level);
int clamp_resist_magnitude(int magnitude);
int apply_resistance(int dam, int magnitude);
int resist_magnitude_for(char_data* victim, int resist_type);
int damage(char_data* attacker, char_data* victim, int dam, int attacktype, int hit_location);
void affect_naked(char_data* ch);
void do_resist_spell(int resist_type, int modifier, char_data* caster, char_data* victim,
    int type, int is_object, const char* str);
const char* room_spell_message_for(int location);

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

/* The one-in-three physical roll has to gate BOTH sides of check_resistances.

   Before magnitudes existed the roll simply zeroed tmp:

       if (number(0,2) == 0 && IS_PHYSICAL(attacktype)) tmp = 0;

   which skipped the resistance branch and the vulnerability branch alike, so a victim
   vulnerable to physical damage felt it on roughly two swings in three. Moving the roll into
   physical_resist_misses and consulting it only under `tmp > 0` left the vulnerability branch
   unguarded and made it fire on every single swing - about 25% more damage against anyone
   carrying V-PHYSICAL, with nothing in the design calling for it.

   The roll is real RNG, so this is asserted statistically. Seeing at least one unboosted swing
   in 300 is what distinguishes correct from broken: with the bug every swing is boosted, and
   the chance of 300 consecutive boosts when the roll works is (2/3)^300, which will not happen
   before the heat death of the universe. The band around it is loose on purpose - it is there
   to catch the odds being changed wholesale, not to police sampling noise. */
TEST(ResistanceDamage, ThePhysicalRollSparesAVulnerableVictimAboutOneSwingInThree)
{
    constexpr int swings = 300;
    constexpr int base_damage = 30;
    constexpr int boosted_damage = base_damage * 3 / 2;
    constexpr int starting_hit = 500;

    ResistDamageContext context;
    context.victim.specials.vulnerability = (1 << RESIST_PHYS);

    int boosted = 0;
    int spared = 0;

    for (int swing = 0; swing < swings; ++swing) {
        context.victim.tmpabilities.hit = starting_hit;

        damage(&context.attacker, &context.victim, base_damage, TYPE_HIT, 0);

        const int taken = starting_hit - context.victim.tmpabilities.hit;
        if (taken == boosted_damage)
            ++boosted;
        else if (taken == base_damage)
            ++spared;
        else
            FAIL() << "a swing landed for " << taken << ", expected " << base_damage
                   << " or " << boosted_damage;
    }

    EXPECT_GT(spared, 0)
        << "every swing was boosted - the physical roll is not gating the vulnerability branch";
    EXPECT_GT(boosted, 0) << "no swing was boosted - the vulnerability never fired at all";

    EXPECT_GT(boosted, swings / 2) << "boosted " << boosted << " of " << swings
                                   << ", expected roughly two thirds";
    EXPECT_LT(boosted, swings * 9 / 10) << "boosted " << boosted << " of " << swings
                                        << ", expected roughly two thirds";
}

/* Weapon hits keep checking the legacy "ungrouped" bit before physical.

   Weapon damage types (130-143) and archery (61) sit below MAX_SKILLS, so the live check reaches
   them through its generic skills[attack_type].skill_spec lookup before it ever gets to its own
   weapon block. Those rows are blank placeholders naming bit 0 (and "defend" - bit 14 - at 131,
   which collides with bludgeon), so on live every weapon swing tests bit 0 first. It is an
   accident of MAX_SKILLS being raised 128 -> 256 in 2018, not a design, and correcting it would
   move 46 live mobs.

   That correction is deliberately NOT part of the resistance work: changing mob toughness in the
   same release would contaminate the before/after comparison of the resistance change itself. The
   legacy order is preserved on purpose, and these tests exist to keep it preserved and to fail
   loudly if it is "fixed" without that being the intent.

   Each case samples many swings rather than one. The one-in-three roll means a single swing at
   full damage is ambiguous - it could be the roll sparing the victim, or the resistance not
   applying at all - and an assertion that accepts both proves nothing. Over 200 swings the
   difference is absolute: a working legacy check reduces roughly two thirds of them, a broken one
   reduces none.

   Scope matters: the pre-check covers ONLY weapon types and archery, the exact set the branch had
   rerouted. Spells and special attacks resolve through skills[].resist as before. */
namespace {

/* How many of `swings` landed for each amount, against a victim the caller has set up. */
std::map<int, int> sample_weapon_swings(char_data* attacker, char_data* victim, int base, int swings,
    int attack_type = TYPE_HIT)
{
    std::map<int, int> taken;
    for (int i = 0; i < swings; ++i) {
        victim->tmpabilities.hit = 500;
        damage(attacker, victim, base, attack_type, 0);
        ++taken[500 - victim->tmpabilities.hit];
    }
    return taken;
}

constexpr int kSwings = 200;

} // namespace

TEST(LegacyWeaponResistOrder, AWeaponHitStillChecksTheUngroupedBitBeforePhysical)
{
    ResistDamageContext context;
    context.victim.specials.resistance = (1 << RESIST_NONE);

    const auto taken = sample_weapon_swings(&context.attacker, &context.victim, 30, kSwings);

    EXPECT_GT(taken.count(20) ? taken.at(20) : 0, 0)
        << "no swing was reduced - the legacy bit-0 check is gone, so bit-0 mobs no longer "
           "resist weapon damage as they do on live";
    EXPECT_EQ(taken.count(45), 0u) << "resistance must never read as vulnerability";
}

TEST(LegacyWeaponResistOrder, TheUngroupedBitStillWorksAsAWeaponVulnerability)
{
    ResistDamageContext context;
    context.victim.specials.vulnerability = (1 << RESIST_NONE);

    const auto taken = sample_weapon_swings(&context.attacker, &context.victim, 30, kSwings);

    EXPECT_GT(taken.count(45) ? taken.at(45) : 0, 0)
        << "no swing did extra damage - the legacy bit-0 vulnerability is gone";
    EXPECT_GT(taken.count(30) ? taken.at(30) : 0, 0)
        << "the one-in-three roll must still spare some swings";
}

TEST(LegacyWeaponResistOrder, TheUngroupedBitWinsOverPhysicalJustAsItDoesLive)
{
    // Checks stop at the first match and the legacy bit is tested first, so a mob that is bit-0
    // resistant AND physically vulnerable resists rather than taking extra.
    ResistDamageContext context;
    context.victim.specials.resistance = (1 << RESIST_NONE);
    context.victim.specials.vulnerability = (1 << RESIST_PHYS);

    const auto taken = sample_weapon_swings(&context.attacker, &context.victim, 30, kSwings);

    EXPECT_EQ(taken.count(45), 0u)
        << "physical vulnerability was reached even though the legacy bit already matched";
    EXPECT_GT(taken.count(20) ? taken.at(20) : 0, 0) << "and the legacy resistance should apply";
}

TEST(LegacyWeaponResistOrder, PhysicalResistanceStillAppliesWhenTheLegacyBitIsClear)
{
    ResistDamageContext context;
    context.victim.specials.resistance = (1 << RESIST_PHYS);

    const auto taken = sample_weapon_swings(&context.attacker, &context.victim, 30, kSwings);

    EXPECT_GT(taken.count(20) ? taken.at(20) : 0, 0)
        << "physical resistance must survive the legacy pre-check";
}

TEST(LegacyWeaponResistOrder, ArcheryFollowsTheSameLegacyOrderAsWeapons)
{
    // Archery is skill 61 - always inside the skill table, so it has checked bit 0 since the
    // original code, not since 2018. It is preserved for the same reason.
    ResistDamageContext context;
    context.victim.specials.resistance = (1 << RESIST_NONE);

    const auto taken
        = sample_weapon_swings(&context.attacker, &context.victim, 30, kSwings, SKILL_ARCHERY);

    EXPECT_GT(taken.count(20) ? taken.at(20) : 0, 0) << "bit 0 must still resist archery";
    EXPECT_EQ(taken.count(30), 0u)
        << "archery is not IS_PHYSICAL, so the one-in-three roll must never spare it";
}

TEST(LegacyWeaponResistOrder, APhysicalMagnitudeStillReducesByItsPercentage)
{
    // The resistance feature itself: a spell- or item-granted physical magnitude applies as a
    // percentage and is not subject to the one-in-three roll. Preserving the legacy order must
    // not cost this.
    ResistDamageContext context;
    context.resist_element(RESIST_PHYS, 30);

    damage(&context.attacker, &context.victim, 50, TYPE_HIT, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 465)
        << "50 physical damage against a 30% physical resistance should land as 35";
}

TEST(LegacyWeaponResistOrder, TheLegacyBitDoesNotLeakIntoSpells)
{
    // The pre-check is scoped to weapons and archery. A fire spell is still judged on fire.
    ResistDamageContext context;
    context.victim.specials.resistance = (1 << RESIST_NONE);

    damage(&context.attacker, &context.victim, 30, SPELL_FIREBOLT, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 470) << "bit 0 must not resist a firebolt";
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

/* A match at the very end of the string used to walk off the end of it.

   After consuming the pattern, i indexes the terminator; the old loop copied str[i] - the NUL
   itself - into result and then let its own i++ step past it, so it carried on reading whatever
   followed the string in memory and appending it to result. The result still *looked* right to
   strcmp, because the NUL it had just copied terminated it, which is why this survived: the
   damage is the out-of-bounds read and the writes past the terminator into the caller's buffer.

   The input therefore carries known bytes after its terminator and the output buffer is filled
   with a sentinel, so the assertion can see writes that strcmp cannot. */
TEST(RemovePattern, StopsAtTheTerminatorWhenThePatternMatchesAtTheEnd)
{
    char pattern[] = "V-";
    const char input[] = { 'F', 'I', 'R', 'E', 'V', '-', '\0', 'Z', 'Z', 'Z', '\0', '\0' };

    char out[64];
    memset(out, '#', sizeof(out));

    remove_pattern(const_cast<char*>(input), out, pattern);

    EXPECT_STREQ(out, "FIRE");
    EXPECT_EQ(out[5], '#')
        << "remove_pattern read past the terminator and appended what followed the string";
}

TEST(SprintbitResistances, UsesTheFlatDefaultWhenNoAffectBacksTheBit)
{
    // Mirrors a mob record or a flag-only APPLY_RESIST item: the bit is set but no affect exists.
    char_data victim {};
    char result[512];

    sprintbit_resistances(&victim, (1 << RESIST_FIRE), resistance_name, result, 33, TRUE);
    EXPECT_STREQ(result, "   fire (33%)\n\r");
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
    sprintbit_resistances(&victim, (1 << RESIST_FIRE), resistance_name, result, 33, TRUE);
    EXPECT_STREQ(result, "   fire (11%)\n\r");
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
    sprintbit_resistances(&victim, (1 << RESIST_FIRE), resistance_name, result, 33, TRUE);
    EXPECT_STREQ(result, "   fire (40%)\n\r");
}

TEST(SprintbitResistances, SkipsBlankSlotsAndStopsAtTheSentinel)
{
    // resistance_name[] has empty-string slots at indices 13-15 before the "\n" terminator;
    // a set bit there must not print a blank line, and the walk must not read past "\n".
    char_data victim {};
    char result[512];

    sprintbit_resistances(&victim, (1 << 13) | (1 << 14) | (1 << 15), resistance_name, result, 33, TRUE);
    EXPECT_STREQ(result, "");
}

TEST(SprintbitResistances, RendersVulnerabilitiesWithoutTheVPrefix)
{
    char_data victim {};
    char result[512];

    sprintbit_resistances(&victim, (1 << RESIST_FIRE), vulnerability_name, result, 50, FALSE);
    EXPECT_STREQ(result, "   fire (50%)\n\r");
}

TEST(SprintbitResistances, AResistAffectNeverSetsTheVulnerablePercentage)
{
    // Reachable in the live world: one object is vulnerable to lightning and item 2075 resists
    // lightning at 30%. Holding both used to print "vulnerable to: lightning (30%)", because the
    // shared renderer scanned APPLY_RESIST affects for the vulnerability table too.
    char_data victim {};
    affected_type resist {};
    resist.location = APPLY_RESIST;
    resist.modifier = RESIST_LGHT;
    resist.effect_modifier = 30;
    resist.next = nullptr;
    victim.affected = &resist;

    char result[512];
    sprintbit_resistances(&victim, (1 << RESIST_LGHT), vulnerability_name, result, 50, FALSE);
    EXPECT_STREQ(result, "   lightning (50%)\n\r")
        << "a resist affect must not be read as the strength of a vulnerability";

    // The same affect is exactly what the resistance table should report.
    sprintbit_resistances(&victim, (1 << RESIST_LGHT), resistance_name, result, 33, TRUE);
    EXPECT_STREQ(result, "   lightning (30%)\n\r");
}

TEST(SprintbitResistances, AgreesWithTheDamagePathBecauseItCallsTheSameLookup)
{
    // Fix 2: the display no longer carries its own copy of the largest-wins rule.
    char_data victim {};
    affected_type strong {};
    affected_type weak {};

    strong.location = APPLY_RESIST;
    strong.modifier = RESIST_COLD;
    strong.effect_modifier = 37;
    strong.next = &weak;

    weak.location = APPLY_RESIST;
    weak.modifier = RESIST_COLD;
    weak.effect_modifier = 12;
    weak.next = nullptr;

    victim.affected = &strong;

    char result[512];
    sprintbit_resistances(&victim, (1 << RESIST_COLD), resistance_name, result, 33, TRUE);

    char expected[64];
    sprintf(expected, "   cold (%d%%)\n\r", resist_magnitude_for(&victim, RESIST_COLD));
    EXPECT_STREQ(result, expected);
}

namespace {

/* A mob with a prototype behind it, which is what affect_naked() has to read its intrinsic
   masks back from. 204 of 3010 mob records carry one. */
struct ProtoMobContext {
    char_data* saved_proto = nullptr;
    char_data proto {};
    char_data mob {};
    char proto_name[16] = "proto_mob";
    char mob_name[16] = "proto_mob";

    ProtoMobContext(int resistances, int vulnerabilities)
    {
        saved_proto = mob_proto;

        proto.specials2.act = MOB_ISNPC;
        proto.player.short_descr = proto_name;
        proto.player.race = RACE_HUMAN;
        proto.specials.resistance = resistances;
        proto.specials.vulnerability = vulnerabilities;
        mob_proto = &proto;

        mob = proto;
        mob.player.short_descr = mob_name;
        mob.nr = 0;
        mob.affected = nullptr;
        mob.in_room = NOWHERE;
    }

    ~ProtoMobContext()
    {
        while (mob.affected)
            affect_remove(&mob, mob.affected);
        mob_proto = saved_proto;
    }
};

/* affected_by_spell() falls back to the head of the list when handed a null start, so it cannot
   be used to ask "is there a second one?". Count instead. */
int count_affects_of_type(const char_data* ch, int type)
{
    int found = 0;
    int count = 0;
    for (affected_type* aff = ch->affected; aff && count < MAX_AFFECT; aff = aff->next, count++) {
        if (aff->type == type)
            ++found;
    }
    return found;
}

} // namespace

TEST(AffectNaked, RestoresAMobsIntrinsicMasksFromItsPrototype)
{
    // affect_total() strips both masks before replaying gear and affects, so whatever the mob
    // owns in its own .mob record has to come back here or it is gone for good.
    ProtoMobContext context((1 << RESIST_FIRE) | (1 << RESIST_DARK), (1 << RESIST_COLD));

    context.mob.specials.resistance = 0;
    context.mob.specials.vulnerability = 0;

    affect_naked(&context.mob);

    EXPECT_EQ(context.mob.specials.resistance, (1 << RESIST_FIRE) | (1 << RESIST_DARK));
    EXPECT_EQ(context.mob.specials.vulnerability, (1 << RESIST_COLD));
}

TEST(AffectNaked, AMobKeepsItsIntrinsicBitWhenAnAffectOnTheSameElementIsRemoved)
{
    // The regression this guards: APPLY_RESIST removal now really does clear the bit, so a mob
    // that is intrinsically fire-resistant and is then given resist fire used to end up with no
    // fire resistance at all once the affect wore off.
    ProtoMobContext context((1 << RESIST_FIRE), 0);

    affected_type newaf {};
    newaf.type = SPELL_RESIST_FIRE;
    newaf.duration = 10;
    newaf.modifier = RESIST_FIRE;
    newaf.location = APPLY_RESIST;
    newaf.bitvector = 0;
    newaf.counter = 0;
    newaf.effect_modifier = 30;

    affect_to_char(&context.mob, &newaf);
    ASSERT_TRUE(context.mob.specials.resistance & (1 << RESIST_FIRE));

    affected_type* applied = affected_by_spell(&context.mob, SPELL_RESIST_FIRE);
    ASSERT_NE(applied, nullptr);
    affect_remove(&context.mob, applied);

    EXPECT_TRUE(context.mob.specials.resistance & (1 << RESIST_FIRE))
        << "the mob record's own fire resistance must survive the affect wearing off";
}

TEST(AffectNaked, APlayerStillLosesEverythingBecauseNothingIsIntrinsic)
{
    char_data player {};
    char_prof_data profs {};
    char name[16] = "resist_pc";
    player.player.name = name;
    player.player.race = RACE_HUMAN;
    player.profs = &profs; // GET_PROF_LEVEL dereferences this for a PC
    player.specials.resistance = (1 << RESIST_FIRE);
    player.specials.vulnerability = (1 << RESIST_COLD);

    affect_naked(&player);

    EXPECT_EQ(player.specials.resistance, 0);
    EXPECT_EQ(player.specials.vulnerability, 0);
}

TEST(ClampResistMagnitude, HoldsAnItemGrantedStrengthInsideZeroToOneHundred)
{
    EXPECT_EQ(clamp_resist_magnitude(0), 0);
    EXPECT_EQ(clamp_resist_magnitude(30), 30);
    EXPECT_EQ(clamp_resist_magnitude(100), 100);
    EXPECT_EQ(clamp_resist_magnitude(101), 100);
    EXPECT_EQ(clamp_resist_magnitude(255), 100) << "the largest level half an item can encode";
    EXPECT_EQ(clamp_resist_magnitude(-5), 0);
}

/* Strength comes from the caster's mystic profession level, not their overall level.

   The two are different numbers whenever the cleric coefficient is not dominant: profession
   level is derived from the coefficient, so a mostly-warrior character can sit at overall level
   30 with a cleric profession level of 10. Keying the magnitude off GET_LEVEL handed that
   dabbler the same 40% ceiling as a dedicated mystic and differed only in duration.

   Deliberately the RAW profession level rather than get_mystic_caster_level(): that helper adds
   a will factor and contains a random roll, so feeding it here would make a resistance's
   strength reroll on every cast and leave `affections` disagreeing with itself. Duration keeps
   using the helper - varying how long it lasts is harmless in a way that varying how much it
   blocks is not. */
/* A player caster. get_prof_level() answers an NPC with its overall level, so the distinction
   this test exists for is only visible on a PC - which also means the change leaves mob casters
   exactly where they were. clear_char allocates the profs block the profession levels live in. */
struct PlayerCasterContext {
    char_data caster {};
    char name[16] = "castbench";

    PlayerCasterContext(int overall_level, int cleric_profession_level)
    {
        clear_char(&caster, 0);
        caster.player.short_descr = name;
        caster.player.race = RACE_HUMAN;
        caster.player.level = overall_level;
        caster.in_room = NOWHERE;
        utils::set_prof_level(PROF_CLERIC, caster, cleric_profession_level);
    }

    ~PlayerCasterContext()
    {
        while (caster.affected)
            affect_remove(&caster, caster.affected);
    }
};

TEST(DoResistSpell, MagnitudeFollowsTheMysticProfessionLevelNotTheOverallLevel)
{
    PlayerCasterContext context(30, 10);

    do_resist_spell(SPELL_RESIST_FIRE, RESIST_FIRE, &context.caster, &context.caster,
        SPELL_TYPE_SPELL, 0, "fire");

    affected_type* cast = affected_by_spell(&context.caster, SPELL_RESIST_FIRE);
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->effect_modifier, 20)
        << "a cleric profession level of 10 is worth 10 + 10, not the overall level's 40% cap";
}

TEST(DoResistSpell, MagnitudeStillReachesTheCapForADedicatedMystic)
{
    PlayerCasterContext context(30, 30);

    do_resist_spell(SPELL_RESIST_FIRE, RESIST_FIRE, &context.caster, &context.caster,
        SPELL_TYPE_SPELL, 0, "fire");

    affected_type* cast = affected_by_spell(&context.caster, SPELL_RESIST_FIRE);
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->effect_modifier, 40) << "min(30, 30) + 10";
}

TEST(DoResistSpell, AMobCasterIsUnaffectedBecauseItsProfessionLevelIsItsLevel)
{
    // get_prof_level() short-circuits for NPCs, so switching the magnitude to the profession
    // level cannot have moved any mob-cast resistance.
    ProtoMobContext context(0, 0);
    context.mob.player.level = 30;

    do_resist_spell(SPELL_RESIST_FIRE, RESIST_FIRE, &context.mob, &context.mob,
        SPELL_TYPE_SPELL, 0, "fire");

    affected_type* cast = affected_by_spell(&context.mob, SPELL_RESIST_FIRE);
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->effect_modifier, 40);
}

TEST(DoResistSpell, AnItemGrantedMagnitudeIsClampedToOneHundred)
{
    // eff_mod is the item's mod/256, so "A 27 25761" would otherwise hand out 100%+ - immunity -
    // where a cast is held to 40%.
    ProtoMobContext context(0, 0);
    const int saved_eff_mod = eff_mod;

    eff_mod = 200;
    do_resist_spell(SPELL_RESIST_FIRE, RESIST_FIRE, &context.mob, &context.mob,
        SPELL_TYPE_SPELL, 1, "fire");

    affected_type* applied = affected_by_spell(&context.mob, SPELL_RESIST_FIRE);
    ASSERT_NE(applied, nullptr);
    EXPECT_EQ(applied->effect_modifier, 100);

    eff_mod = saved_eff_mod;
}

TEST(DoResistSpell, AnItemTakesTheSlotFromACastAndAnUnequipClearsIt)
{
    // The slot-ownership rule: one affect per element at a time, an item always wins the slot,
    // and taking the item off leaves nothing behind.
    ProtoMobContext context(0, 0);
    const int saved_eff_mod = eff_mod;

    context.mob.player.level = 10;

    // A cast takes the slot.
    do_resist_spell(SPELL_RESIST_FIRE, RESIST_FIRE, &context.mob, &context.mob,
        SPELL_TYPE_SPELL, 0, "fire");
    affected_type* cast = affected_by_spell(&context.mob, SPELL_RESIST_FIRE);
    ASSERT_NE(cast, nullptr);
    EXPECT_EQ(cast->effect_modifier,
        cast_resist_magnitude(utils::get_prof_level(PROF_CLERIC, context.mob)));
    EXPECT_GT(cast->duration, 0) << "a cast expires";

    // Wearing the item replaces it: exactly one affect, the item's magnitude, permanent.
    eff_mod = 25;
    do_resist_spell(SPELL_RESIST_FIRE, RESIST_FIRE, &context.mob, &context.mob,
        SPELL_TYPE_SPELL, 1, "fire");

    affected_type* item = affected_by_spell(&context.mob, SPELL_RESIST_FIRE);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->effect_modifier, 25);
    EXPECT_EQ(item->duration, -1) << "an item-held resistance does not tick down";
    EXPECT_EQ(count_affects_of_type(&context.mob, SPELL_RESIST_FIRE), 1)
        << "the cast must have been stripped, not left alongside the item";
    EXPECT_TRUE(context.mob.specials.resistance & (1 << RESIST_FIRE));

    // Casting again while the item holds the slot changes nothing.
    do_resist_spell(SPELL_RESIST_FIRE, RESIST_FIRE, &context.mob, &context.mob,
        SPELL_TYPE_SPELL, 0, "fire");
    item = affected_by_spell(&context.mob, SPELL_RESIST_FIRE);
    ASSERT_NE(item, nullptr);
    EXPECT_EQ(item->effect_modifier, 25) << "the item keeps the slot it owns";
    EXPECT_EQ(count_affects_of_type(&context.mob, SPELL_RESIST_FIRE), 1);

    // Taking the item off clears the slot outright.
    do_resist_spell(SPELL_RESIST_FIRE, RESIST_FIRE, &context.mob, &context.mob,
        SPELL_TYPE_ANTI, 1, "fire");
    EXPECT_EQ(affected_by_spell(&context.mob, SPELL_RESIST_FIRE), nullptr);
    EXPECT_FALSE(context.mob.specials.resistance & (1 << RESIST_FIRE))
        << "nothing is left holding fire resistance";

    eff_mod = saved_eff_mod;
}

/* spell_wear_off_msg[] is indexed by spell number but is far shorter than MAX_SKILLS, which is
   the only bound affect_update_person() checks. Before the guard below existed, a cast resist
   affect running out read past the end of the table and segfaulted the whole game loop. */
TEST(SpellWearOffMessage, RefusesSpellNumbersPastTheEndOfTheTable)
{
    extern const int spell_wear_off_msg_count;

    EXPECT_EQ(spell_wear_off_message(spell_wear_off_msg_count), nullptr)
        << "the first index past the table";
    EXPECT_EQ(spell_wear_off_message(MAX_SKILLS - 1), nullptr)
        << "MAX_SKILLS is the bound the callers used to trust, and it is far past the table";
    EXPECT_EQ(spell_wear_off_message(-1), nullptr);

    EXPECT_NE(spell_wear_off_message(0), nullptr) << "the first real entry";
    EXPECT_NE(spell_wear_off_message(spell_wear_off_msg_count - 1), nullptr)
        << "the last real entry";
}

TEST(SpellWearOffMessage, EveryResistSpellHasAWearOffLine)
{
    const int resist_spells[] = { SPELL_RESIST_FIRE, SPELL_RESIST_COLD, SPELL_RESIST_LIGHT,
        SPELL_RESIST_ILLUSION, SPELL_RESIST_PHYSICAL, SPELL_RESIST_DARK };

    for (int spell : resist_spells) {
        const char* message = spell_wear_off_message(spell);
        ASSERT_NE(message, nullptr) << "spell " << spell << " indexes past the table";
        EXPECT_NE(*message, '\0') << "spell " << spell << " has no wear-off line";
    }
}

TEST(SpellWearOffNotify, SurvivesAnAffectWhoseTypeHasNoTableEntry)
{
    /* The exact shape of the crash: affect_update_person() hands affect_remove_notify() an
       expiring affect whose type cleared its MAX_SKILLS test but sits past the table's end. */
    ProtoMobContext context(0, 0);

    affected_type af {};
    af.type = MAX_SKILLS - 1;
    af.duration = 0;
    af.modifier = 0;
    af.location = APPLY_NONE;
    af.bitvector = 0;
    af.counter = 0;

    affect_to_char(&context.mob, &af);
    affected_type* applied = context.mob.affected;
    ASSERT_NE(applied, nullptr);
    ASSERT_EQ(applied->type, MAX_SKILLS - 1);

    affect_remove_notify(&context.mob, applied);

    EXPECT_EQ(context.mob.affected, nullptr)
        << "the affect is still removed, just without a wear-off line";
}

/* room_spell_message[] is indexed by spell number but is far shorter than MAX_SKILLS, which is
   the only bound show_room_affection() checked. A room affect whose location landed at 128 or
   above would read past the end of the table and segfault. */
TEST(RoomSpellMessage, RefusesLocationsPastTheEndOfTheTable)
{
    extern const int room_spell_message_count;

    EXPECT_EQ(room_spell_message_for(room_spell_message_count), nullptr)
        << "the first index past the table";
    EXPECT_EQ(room_spell_message_for(MAX_SKILLS - 1), nullptr)
        << "MAX_SKILLS is the bound the caller used to trust, and it is far past the table";
    EXPECT_EQ(room_spell_message_for(-1), nullptr);

    EXPECT_NE(room_spell_message_for(0), nullptr) << "the first real entry";
    EXPECT_NE(room_spell_message_for(room_spell_message_count - 1), nullptr)
        << "the last real entry";
}

TEST(RoomSpellMessage, EmptySlotsAreStillPresentJustBlank)
{
    // room_spell_message[0] is an empty string, not a missing entry, so callers must keep
    // filtering it on *message the way show_room_affection() always has.
    const char* message = room_spell_message_for(0);
    ASSERT_NE(message, nullptr);
    EXPECT_EQ(*message, '\0');
}

TEST(RoomSpellMessage, KnownRoomAffectsHaveAMessage)
{
    // SPELL_HAZE (52) and SPELL_NONE (127) also reach show_room_affection() as a room affect's
    // location, but their slots are blank in the table today, so they are excluded here.
    const int room_spells[] = { SPELL_POISON, SPELL_MIST_OF_BAAZUNGA, SPELL_BLAZE };

    for (int spell : room_spells) {
        const char* message = room_spell_message_for(spell);
        ASSERT_NE(message, nullptr) << "spell " << spell << " indexes past the table";
        EXPECT_NE(*message, '\0') << "spell " << spell << " has no room message";
    }
}
