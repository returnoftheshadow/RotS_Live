#include "gtest/gtest.h"

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
    EXPECT_EQ(cast->effect_modifier, cast_resist_magnitude(GET_LEVEL(&context.mob)));
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
