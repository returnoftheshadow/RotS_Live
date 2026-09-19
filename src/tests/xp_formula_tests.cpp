// Pins the live XP formulas at the research tiers from
// .superpowers/sdd/2026-09-19-xp-progression-research/, so the Python mirror in
// tools/xp_research can be validated against the server and so a later progression redesign has
// to update these numbers deliberately.
//
// Three formulas from the same trace are NOT pinned here, and are pinned in Python only
// (tools/xp_research/test_xp_formulas.py):
//   - the per-hit melee/mental formula (`hit_xp_melee`/`hit_xp_mental`, FORMULAS.md), which lives
//     inline inside damage_credited() (src/fight.cpp:1836) and do_mental() (src/clerics.cpp:89) --
//     driving it for real means a live combat round through functions with unrelated side
//     effects far outside XP (hit rolls, damage application, death checks).
//   - flee loss (`flee_loss`), inline inside do_flee() (src/act_offe.cpp:338-390).
//   - death loss (`death_loss`), inline inside die() (src/fight.cpp:1010-1330), which calls
//     raw_kill() and, for a real PC victim, save_char()/Crash_crashsave() -- real player-file
//     writes this suite has no sandbox for (see fight_credit_tests.cpp's own file comment on the
//     same "no extract_char seam for a PC" constraint).
//
// Fixture note on the age formula (src/fight.cpp:1354-1360): MOB_AGE_TICKS(mob, time(0))
// (src/utils.h:677) is the RAW mud-hour age; exp_with_modifiers()'s own local `age` is a second,
// DERIVED value -- MOB_AGE_TICKS * 40 / (GET_LEVEL(dead_man) + 20) -- that only equals
// average_mob_life when GET_LEVEL(dead_man) + 20 == 40, i.e. a level-20 mob. Every mob fixture
// below is pinned to MOB_AGE_TICKS == average_mob_life (40) exactly as the task brief specifies,
// but the DERIVED `age` therefore differs by mob level (45 for a level-15 mob, 20 for a level-60
// mob) -- both are worked out explicitly in the comments below rather than assumed to be 40.
#include "../limits.h"
#include "../structs.h"
#include "../utils.h"
#include "../zone.h"
#include <ctime>
#include <gtest/gtest.h>

// exp_with_modifiers() has no header declaration -- fight.cpp has no fight.h -- so it is declared
// extern here exactly as fight_credit_tests.cpp already does for the equally header-less
// group_gain() (src/tests/fight_credit_tests.cpp:919).
int exp_with_modifiers(char_data* character, char_data* dead_man, int base_exp);

extern room_data world;
extern int top_of_world;

// Definition is `int average_mob_life = 40;` (src/config.cpp:33); declared non-const here to
// match that definition, the same way src/db.cpp:1539 and src/act_info.cpp:2695 declare it
// (src/fight.cpp:57's own `extern const int` is a pre-existing mismatch in that file, not
// mirrored here).
extern int average_mob_life;

namespace {

void ensure_test_world(int minimum_room_number)
{
    if (!room_data::BASE_WORLD)
    {
        world.create_bulk(minimum_room_number + 2);
        top_of_world = minimum_room_number + 1;
    }
    else if (top_of_world < minimum_room_number)
    {
        top_of_world = minimum_room_number;
    }
}

// Room number this file claims within the shared test-binary world[] -- an out-of-band value
// distinct from every other suite's claimed rooms (fight_credit_tests.cpp: 900-907;
// room_affect_caster_tests.cpp: 950-953; room_affect_tick_tests.cpp: 962-990;
// summon_targeting_tests.cpp: 1000-1001).
constexpr int kXpFormulaRoom = 1100;

// exp_with_modifiers()'s east-of-the-river bonus (src/fight.cpp:1386-1387) reads
// zone_table[world[character->in_room].zone].x whenever RACE_GOOD(character) is true -- which is
// unconditionally, for every "good-race good-aligned killer" fixture in this file, regardless of
// whether a given test cares about the bonus. This test binary never boots a real zone_table (see
// mage_tests.cpp's ZoneTableGuard for the same constraint elsewhere), so every test below installs
// this single-entry stub for its scope and restores whatever was installed before (normally
// nullptr).
struct ZoneTableGuard
{
    zone_data* previous_table; // real zone_table found before the test; restored on scope exit
    int previous_top; // real top_of_zone_table found before the test; restored on scope exit
    zone_data stub[1] {}; // the one-zone stub table installed for the scope

    ZoneTableGuard()
        : previous_table(zone_table)
        , previous_top(top_of_zone_table)
    {
        ensure_test_world(kXpFormulaRoom);
        world[kXpFormulaRoom].zone = 0;
        zone_table = stub;
        top_of_zone_table = 0;
    }

    ~ZoneTableGuard()
    {
        zone_table = previous_table;
        top_of_zone_table = previous_top;
    }
};

// A good-race (human, RACE_HUMAN == 1; race_is_good() treats race 1..9 as good --
// src/utils.h:631, src/structs.h:870), good-aligned (alignment >= 100 satisfies IS_GOOD(),
// src/utils.h:657) PC killer, standing in the room this file claims. `specials2.act` stays 0 (its
// char_data{} default) -- a PC, not an NPC.
void init_good_killer(char_data& killer, int level)
{
    killer.player.race = RACE_HUMAN;
    killer.player.level = level;
    killer.specials2.alignment = 1000;
    killer.in_room = kXpFormulaRoom;
}

// A neutral (alignment 0, so IS_GOOD()/IS_EVIL() are both false), unflagged, standing NPC at the
// given level, aged to exactly average_mob_life raw MOB_AGE_TICKS (src/utils.h:677) -- the
// "neutral age" fixture the task brief calls for (see this file's header comment on how the
// DERIVED `age` inside exp_with_modifiers differs from this raw tick count). Difficulty
// (GET_DIFFICULTY() == specials.prompt_number, src/utils.h:403) stays 0, so step 7's difficulty
// scaling never applies.
void init_neutral_standing_mob(char_data& mob, int level)
{
    mob.specials2.act = MOB_ISNPC;
    mob.player.race = RACE_HUMAN;
    mob.player.level = level;
    mob.specials.default_pos = POSITION_STANDING;
    mob.specials.prompt_number = 0;
    mob.specials2.alignment = 0;
    // time()/time_t is the legacy API MOB_AGE_TICKS() itself is written against
    // (src/utils.h:677); this fixture drives that EXISTING production macro's exact behavior
    // rather than introducing new production code, so it does not fall under this depot's
    // new-code std::chrono preference (see CLAUDE.local.md).
    mob.player.time.logon = time(nullptr) - static_cast<time_t>(average_mob_life) * SECS_PER_MUD_HOUR;
}

} // namespace

TEST(XpFormula, LevelCostIsQuadratic)
{
    // xp_to_level(lvl) = lvl * lvl * 1500 (src/limits.cpp:90).
    // 30 * 30 * 1500 = 900 * 1500 = 1,350,000.
    EXPECT_EQ(xp_to_level(30), 1350000) << "tier: level 30 (legend threshold) cost";
    // 90 * 90 * 1500 = 8100 * 1500 = 12,150,000.
    EXPECT_EQ(xp_to_level(90), 12150000) << "tier: level 90 (top mortal level) cost";
}

namespace {

// A level-60 PC parked well below every gain_exp() clamp threshold (GET_LEVEL < LEVEL_IMMORT - 1
// == 90 for a positive gain, < LEVEL_IMMORT == 91 for a negative one -- src/limits.cpp:107-124),
// so gain_exp() always reaches gain_exp_regardless(). `profs` is a real (zeroed) char_prof_data:
// GET_PROF_LEVEL()/GET_PROF_COOF() (src/utils.h:317-334) dereference character.profs
// unconditionally for a non-NPC, non-PROF_GENERAL lookup, and gain_exp_regardless()'s
// affect_total() tail (src/limits.cpp:474-478, reached whenever a negative gain runs) walks
// through recalc_abilities() -> class_HP() -> utils::get_prof_points(), which does exactly that
// (src/char_utils.cpp:385-392) -- a null profs pointer here would segfault.
struct LevelSixtyPc
{
    char_data character {}; // the level-60 PC under test
    char_prof_data profs {}; // character.profs target -- required so the affect_total() tail
                              // reached by a negative gain_exp() doesn't dereference a null profs
                              // pointer (see the class comment above)

    explicit LevelSixtyPc(int starting_exp)
    {
        character.profs = &profs;
        character.player.race = RACE_HUMAN;
        character.player.level = 60;
        character.points.exp = starting_exp;
        // Pinned comfortably above the point where gain_exp_regardless()'s mini-level loop
        // (src/limits.cpp:444-453) would advance for either clamp case below: the loop's own
        // condition, temp*temp*3/20 <= exp, is already false at temp == mini_level for every exp
        // this fixture reaches (max 5,407,000; 6100*6100*3/20 = 5,581,500 > 5,407,000), so the
        // loop body never runs.
        character.specials2.mini_level = 6100;
    }
};

} // namespace

TEST(XpFormula, GainExpClampsAPositiveGainToSevenThousand)
{
    // xp_to_level(60) = 60*60*1500 = 5,400,000.
    LevelSixtyPc pc(5400000);

    // gain_exp(ch, 250000): GET_LEVEL(60) < LEVEL_IMMORT-1 (90), so
    // gain = MIN(7000, 250000) = 7000 (src/limits.cpp:107-110); gain_exp_regardless() then adds
    // it directly: 5,400,000 + 7000 = 5,407,000.
    gain_exp(&pc.character, 250000);

    EXPECT_EQ(GET_EXP(&pc.character), 5407000)
        << "tier: level 60 PC; a +250000 event must clamp to the +7000 single-event cap";
}

TEST(XpFormula, GainExpClampsANegativeGainToTenThousandWithoutDeleveling)
{
    // xp_to_level(60) = 5,400,000.
    LevelSixtyPc pc(5400000);

    // gain_exp(ch, -250000): GET_LEVEL(60) < LEVEL_IMMORT (91), so
    // gain = MAX(-10000, -250000) = -10000 (src/limits.cpp:113-116); gain_exp_regardless()
    // subtracts it: 5,400,000 - 10000 = 5,390,000. The delevel loop's own condition
    // (src/limits.cpp:461) is xp_to_level(60) - 20000 > exp, i.e. 5,380,000 > 5,390,000, which is
    // false, so the loop never runs and the level stays 60.
    gain_exp(&pc.character, -250000);

    EXPECT_EQ(GET_EXP(&pc.character), 5390000)
        << "tier: level 60 PC; a -250000 event must clamp to the -10000 single-event floor";
    EXPECT_EQ(GET_LEVEL(&pc.character), 60)
        << "tier: level 60 PC; the -10000 clamp must stay inside the delevel loop's "
           "20000-point tolerance and not delevel the character";
}

TEST(XpFormula, KillModifiersForALevelNinetyAndALevelThirtyKillerOnALevelFifteenMob)
{
    ZoneTableGuard zone_table_guard;
    zone_table_guard.stub[0].x = 8; // no east bonus (river bonus needs x > 8 -- step 8)

    char_data mob {};
    init_neutral_standing_mob(mob, 15);

    // group_gain()'s solo-kill share (src/fight.cpp:1490-1541, FORMULAS.md "Kill share"), worked
    // by hand for mob_exp = 3930, per the task brief:
    //   levelb(killer) = min(level, 20 + level/3)              (GET_LEVELB, src/utils.h:315)
    //   level_total    = 2 * levelb                             (solo: attacked_level == levelb)
    //   share          = 3930/10 * 2 / 1 / level_total          (num_killers == 1)
    //   base           = share * levelb                         (group_bonus == 0 for a solo kill)

    // --- killer level 90 ---
    // levelb(90) = min(90, 20 + 90/3) = min(90, 50) = 50; level_total = 100.
    // share = (3930/10) * 2 / 1 / 100 = 393 * 2 / 1 / 100 = 786 / 100 = 7; base = 7 * 50 = 350.
    char_data killer_ninety {};
    init_good_killer(killer_ninety, 90);

    // exp_with_modifiers(killer_ninety, mob, 350) (src/fight.cpp:1334-1390):
    //   step 2: base /= max(91, 13) = 91              -> base = 350/91 = 3
    //   step 3: mob is NPC, continue
    //   step 4: 15+6=21 < 90, so base = 6*3/(90-15) = 18/75 = 0
    //   exp = base = 0; every remaining step multiplies 0 (or 0-derived base_exp) by something
    //   and stays 0 -- age, flags, alignment, difficulty, the east bonus and the TEMPORARY bonus
    //   are all no-ops on a zero base.
    EXPECT_EQ(exp_with_modifiers(&killer_ninety, &mob, 350), 0)
        << "tier: level 90 killer, level 15 mob; the level-gap divide (step 4) crushes the "
           "already-small share to zero before any later modifier can apply";

    // --- killer level 30 ---
    // levelb(30) = min(30, 20 + 30/3) = min(30, 30) = 30; level_total = 60.
    // share = 393 * 2 / 1 / 60 = 786 / 60 = 13; base = 13 * 30 = 390.
    char_data killer_thirty {};
    init_good_killer(killer_thirty, 30);

    // exp_with_modifiers(killer_thirty, mob, 390):
    //   step 2: base /= max(31, 13) = 31              -> base = 390/31 = 12
    //   step 4: 15+6=21 < 30, so base = 6*12/(30-15) = 72/15 = 4; exp = 4
    //   step 5 (age): MOB_AGE_TICKS == average_mob_life == 40 (fixture); mob level 15, so the
    //     DERIVED age = 40*40/(15+20) = 1600/35 = 45. Since mob level (15) > 5 and 45 >= 40, the
    //     ELSE branch applies: exp = 4 * (140 - 40*40/45) / 100 = 4 * (140 - 35) / 100
    //     = 4*105/100 = 420/100 = 4
    //   steps 6-7: no flags set, mob not good-aligned, difficulty 0 -- no change
    //   step 8 (east bonus): zone x == 8, not > 8 -- no change
    //   step 9 (TEMPORARY): exp += 2*4/max(1, 29) = 8/29 = 0 -- no change
    EXPECT_EQ(exp_with_modifiers(&killer_thirty, &mob, 390), 4)
        << "tier: level 30 killer, level 15 mob";
}

TEST(XpFormula, KillModifiersForALevelMatchedLevelSixtyKill)
{
    ZoneTableGuard zone_table_guard;
    zone_table_guard.stub[0].x = 8; // no east bonus

    char_data mob {};
    init_neutral_standing_mob(mob, 60);

    char_data killer {};
    init_good_killer(killer, 60);

    // levelb(60) = min(60, 20 + 60/3) = min(60, 40) = 40; level_total = 80.
    // share = 393 * 2 / 1 / 80 = 786 / 80 = 9; base = 9 * 40 = 360.

    // exp_with_modifiers(killer, mob, 360):
    //   step 2: base /= max(61, 58) = 61              -> base = 360/61 = 5
    //   step 4: mob level + 6 = 66 < killer level 60? no -- base stays 5; exp = 5
    //   step 5 (age): MOB_AGE_TICKS == 40 (fixture); mob level 60, so the DERIVED
    //     age = 40*40/(60+20) = 1600/80 = 20. Since mob level (60) > 5 and 20 < 40, the IF branch
    //     applies: exp = 5 * (40*60 + 20*40) / (40*100) = 5 * (2400+800) / 4000
    //     = 5*3200/4000 = 16000/4000 = 4
    //   steps 6-9: no flags set, mob not good-aligned, difficulty 0, zone x == 8 (no east bonus);
    //     TEMPORARY: exp += 2*4/max(1, 59) = 8/59 = 0 -- no change
    EXPECT_EQ(exp_with_modifiers(&killer, &mob, 360), 4)
        << "tier: level-matched kill, killer and mob both level 60 -- sits beside the "
           "level 90/30-vs-15 low-level pins above";
}

TEST(XpFormula, EastOfTheRiverAddsUpToFifteenPercentForGoodRaces)
{
    // NOTE: an earlier version of this test reused the level-30-killer / level-15-mob fixture
    // (base 390, pre-bonus exp 4) from KillModifiersForALevelNinetyAndALevelThirtyKillerOnALevelFifteenMob.
    // That fixture cannot distinguish x = 8 from x = 13: 4 * 15 / 100 truncates to 0 under C++
    // integer division, so both zone values pinned the identical result even if the east-bonus
    // code (src/fight.cpp:1386-1387) were deleted -- the test could not fail for the branch it
    // named. A level-20 killer against the same level-15 mob produces a large enough pre-bonus
    // exp (18) for the 15 percent bonus to survive truncation, so that fixture is used below
    // instead as the distinguishing pair.
    char_data mob {};
    init_neutral_standing_mob(mob, 15);

    ZoneTableGuard zone_table_guard;

    // --- killer level 20: the distinguishing pair ---
    char_data killer_twenty {};
    init_good_killer(killer_twenty, 20);

    // group_gain()'s solo-kill share (src/fight.cpp:1490-1541), worked by hand for mob_exp = 3930:
    //   levelb(20) = min(20, 20 + 20/3) = min(20, 26) = 20; level_total = 2*20 = 40.
    //   share = (3930/10) * 2 / 1 / 40 = 393 * 2 / 1 / 40 = 786 / 40 = 19; base = 19 * 20 = 380.

    // exp_with_modifiers(killer_twenty, mob, 380) (src/fight.cpp:1334-1390):
    //   step 2: base /= max(21, 13) = 21              -> base = 380/21 = 18
    //   step 4: mob level + 6 = 21 < killer level 20? no -- base stays 18; exp = 18
    //   step 5 (age): MOB_AGE_TICKS == average_mob_life == 40 (fixture); mob level 15, so the
    //     DERIVED age = 40*40/(15+20) = 1600/35 = 45. Since mob level (15) > 5 and 45 >= 40, the
    //     ELSE branch applies: exp = 18 * (140 - 40*40/45) / 100 = 18 * (140-35) / 100
    //     = 18*105/100 = 1890/100 = 18
    //   steps 6-7: no flags set, mob not good-aligned, difficulty 0 -- no change
    zone_table_guard.stub[0].x = 8; // <= 8: no east bonus
    // step 8: zone x == 8, not > 8 -- no change; exp stays 18
    // step 9 (TEMPORARY): exp += 2*18/max(1, 19) = 36/19 = 1 -> exp = 19
    int baseline = exp_with_modifiers(&killer_twenty, &mob, 380);
    EXPECT_EQ(baseline, 19) << "tier: level 20 killer, level 15 mob, zone x = 8 (river baseline)";

    // step 8: RACE_GOOD(killer_twenty) is true and zone x(13) > 8, so
    //   exp += exp * min(13-8, 5) * 3 / 100 = exp * 15 / 100.
    // Going into step 8, exp is 18 (identical derivation to the baseline above), so
    //   exp += 18 * 15 / 100 = 270/100 = 2 (C++ integer division) -> exp = 20
    // step 9 (TEMPORARY): exp += 2*20/max(1, 19) = 40/19 = 2 -> exp = 22
    zone_table_guard.stub[0].x = 13; // min(13-8, 5)*3 = 15 percent
    int with_east_bonus = exp_with_modifiers(&killer_twenty, &mob, 380);
    EXPECT_EQ(with_east_bonus, 22)
        << "tier: level 20 killer, level 15 mob, zone x = 13; the +15 percent east bonus is now "
           "visible against the x = 8 baseline (19 -> 22)";

    // --- killer level 90: kept as a second, contrasting pair documenting a DIFFERENT kind of
    // truncation than the one the level-30 fixture above wrongly relied on. Here base is already
    // crushed to 0 by the level-gap divide (step 4, see
    // KillModifiersForALevelNinetyAndALevelThirtyKillerOnALevelFifteenMob above for the full
    // derivation), before the east bonus is ever reached -- multiplying zero by any percentage
    // stays zero, so x = 8 and x = 13 are identical here too, but because the input to step 8 is
    // already zero, not because a nonzero percentage got rounded away.
    char_data killer_ninety {};
    init_good_killer(killer_ninety, 90);

    zone_table_guard.stub[0].x = 8;
    int ninety_baseline = exp_with_modifiers(&killer_ninety, &mob, 350);
    EXPECT_EQ(ninety_baseline, 0) << "tier: level 90 killer, level 15 mob, zone x = 8";

    zone_table_guard.stub[0].x = 13;
    int ninety_with_bonus = exp_with_modifiers(&killer_ninety, &mob, 350);
    EXPECT_EQ(ninety_with_bonus, 0)
        << "tier: level 90 killer, level 15 mob, zone x = 13; base is already zero after the "
           "level-gap divide (step 4), so the east bonus has nothing to multiply";
}

TEST(XpFormula, GoodKillingGoodTakesTwoThirds)
{
    // Same level-30-killer / level-15-mob fixture and base (390) as the east-bonus test above,
    // with the mob's alignment raised into IS_GOOD() range (alignment >= 100 -- src/utils.h:657)
    // instead of the zone's x coordinate.
    ZoneTableGuard zone_table_guard;
    zone_table_guard.stub[0].x = 8; // no east bonus -- isolates the good-on-good branch (step 6)

    char_data neutral_mob {};
    init_neutral_standing_mob(neutral_mob, 15);
    char_data good_mob {};
    init_neutral_standing_mob(good_mob, 15);
    good_mob.specials2.alignment = 1000; // IS_GOOD(good_mob) is now true

    char_data killer {};
    init_good_killer(killer, 30);

    int neutral_result = exp_with_modifiers(&killer, &neutral_mob, 390);
    EXPECT_EQ(neutral_result, 4)
        << "tier: level 30 killer, level 15 neutral mob (baseline before the good-on-good "
           "branch)";

    // step 6: IS_GOOD(killer) && IS_GOOD(good_mob) is now true, so exp = exp * 2 / 3.
    // Going into step 6, exp is 4 (identical derivation to the neutral case above), so
    //   exp = 4 * 2 / 3 = 8 / 3 = 2 (C++ integer division).
    int good_on_good_result = exp_with_modifiers(&killer, &good_mob, 390);
    EXPECT_EQ(good_on_good_result, 2)
        << "tier: level 30 killer, level 15 good-aligned mob; the good-on-good branch takes "
           "two thirds of the neutral-mob result (4 -> 2)";
}
