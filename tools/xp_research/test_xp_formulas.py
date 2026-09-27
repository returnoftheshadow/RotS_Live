"""Unit tests for tools/xp_research/xp_formulas.py.

Every test's expected value is worked by hand from tools/xp_research/FORMULAS.md, or, for the
exp_with_modifiers reconciliation tests, copied verbatim from the hand-derived comments in
src/tests/xp_formula_tests.cpp, which this module must reproduce exactly.
"""
import unittest

from parse_mobs import MobRecord
from xp_formulas import (
    cdiv,
    death_loss,
    exp_with_modifiers,
    flee_loss,
    gain_exp_clamp,
    hit_xp_melee,
    hit_xp_mental,
    kill_share,
    levelb,
    next_level_cost,
    solo_kill_xp,
    xp_to_level,
)


def _mob(level: int, exp: int, alignment: int = 0, mob_flags: int = 0,
         default_pos: int = 8, position: int = 8, prog: int = 0) -> MobRecord:
    # Field values not exercised by the formula module are filler; only level, exp, alignment,
    # mob_flags, position, default_pos and prog feed exp_with_modifiers
    # (FORMULAS.md, "exp_with_modifiers").
    return MobRecord(
        vnum=0, zone=0, aliases="", short_descr="", mob_flags=mob_flags, affected_by=0,
        alignment=alignment, level=level, ob=0, parry=0, dodge=0, hit=0, max_hit=0, damage=0,
        ene_regen=0, gold=0, exp=exp, position=position, default_pos=default_pos, sex=0, race=0,
        prog=prog, spirit=0,
    )


class CdivTruncatesTowardZero(unittest.TestCase):
    def test_matches_floor_division_for_positive_operands(self):
        self.assertEqual(cdiv(7, 2), 3)

    def test_truncates_toward_zero_for_a_negative_numerator(self):
        # C++ -7 / 2 == -3 (truncation), whereas Python's // would floor to -4.
        self.assertEqual(cdiv(-7, 2), -3)

    def test_truncates_toward_zero_for_a_negative_denominator(self):
        self.assertEqual(cdiv(7, -2), -3)

    def test_matches_c_plus_plus_for_the_death_loss_pin(self):
        # -(5_400_000 - 3000) / 62 = -5_397_000 / 62 = -87_048.387... -> -87_048.
        self.assertEqual(cdiv(-5_397_000, 62), -87_048)


class LevelCurve(unittest.TestCase):
    def test_xp_to_level_is_quadratic(self):
        # src/limits.cpp:90: return lvl * lvl * 1500.
        self.assertEqual(xp_to_level(30), 1_350_000)
        self.assertEqual(xp_to_level(90), 12_150_000)

    def test_next_level_cost_at_each_tier(self):
        # next_level_cost(30) = xp_to_level(31) - xp_to_level(30) = 1_441_500 - 1_350_000.
        self.assertEqual(next_level_cost(30), 91_500)
        # next_level_cost(90) = xp_to_level(91) - xp_to_level(90) = 12_421_500 - 12_150_000.
        self.assertEqual(next_level_cost(90), 271_500)

    def test_levelb_caps_at_twenty_plus_a_third(self):
        # src/utils.h:315: min(level, LEVEL_MAX * 2 / 3 + level / 3) = min(level, 20 + level / 3).
        self.assertEqual(levelb(30), 30)
        self.assertEqual(levelb(60), 40)
        self.assertEqual(levelb(90), 50)


class GainClamp(unittest.TestCase):
    """Reproduces src/tests/xp_formula_tests.cpp's GainExpClampsAPositiveGainToSevenThousand and
    GainExpClampsANegativeGainToTenThousandWithoutDeleveling pins, both fixtured at a level-60 PC
    (LevelSixtyPc), plus the level-89/90/91 gate boundary from src/limits.cpp:410-426."""

    def test_positive_events_cap_at_7000(self):
        # LevelSixtyPc pin: GET_LEVEL(60) < LEVEL_IMMORT-1 (90), so MIN(7000, 250000) = 7000.
        self.assertEqual(gain_exp_clamp(250_000, level=60), 7000)

    def test_negative_events_cap_at_minus_10000(self):
        # LevelSixtyPc pin: GET_LEVEL(60) < LEVEL_IMMORT (91), so MAX(-10000, -250000) = -10000.
        self.assertEqual(gain_exp_clamp(-250_000, level=60), -10_000)

    def test_gains_below_the_cap_pass_through_unchanged(self):
        self.assertEqual(gain_exp_clamp(4, level=60), 4)
        self.assertEqual(gain_exp_clamp(-4, level=60), -4)

    def test_positive_gain_is_zero_at_level_ninety(self):
        # src/limits.cpp:416: gain > 0 && GET_LEVEL(ch) < LEVEL_IMMORT - 1 (90) -- false at 90, so
        # gain_exp_regardless is never called and the gain is entirely lost.
        self.assertEqual(gain_exp_clamp(250_000, level=90), 0)

    def test_positive_gain_still_clamps_at_the_top_earning_level_eighty_nine(self):
        # GET_LEVEL(89) < 90 is true, so the ordinary 7000 clamp still applies.
        self.assertEqual(gain_exp_clamp(250_000, level=89), 7000)

    def test_negative_gain_still_applies_at_level_ninety(self):
        # src/limits.cpp:421: gain < 0 && GET_LEVEL(ch) < LEVEL_IMMORT (91) -- true at 90, so
        # losses still land even though gains no longer do.
        self.assertEqual(gain_exp_clamp(-250_000, level=90), -10_000)

    def test_negative_gain_is_zero_at_level_ninety_one(self):
        self.assertEqual(gain_exp_clamp(-250_000, level=91), 0)


class HittingXp(unittest.TestCase):
    def test_level_90_hitting_a_level_15_mob_at_the_cap(self):
        # (1+15) * min(20+180, 400) / (1+90) = 16*200/91 = 3200/91 = 35.16... -> 35.
        self.assertEqual(hit_xp_melee(90, 15, 400), 35)

    def test_mental_attack_multiplies_damage_by_five_before_the_cap(self):
        # damg=40 -> damg*5=200, same as the melee cap case above: 16*200/91 = 35.
        self.assertEqual(hit_xp_mental(90, 15, 40), 35)


class Losses(unittest.TestCase):
    def test_flee_loss_is_the_sum_of_levels(self):
        # src/act_offe.cpp:388-390: loose = L_fleeing + L_opponent, applied as gain_exp(ch, -loose).
        # flee_loss reports the positive magnitude of that loss:
        # flee_loss(75, 15) == 90 (75 + 15), not the signed exp delta.
        self.assertEqual(flee_loss(75, 15), 90)

    def test_death_loss_tenth_and_full(self):
        # base = -(exp - 3000) / (level + 2) with C++ truncation toward zero.
        # At level 60 with exp = xp_to_level(60) = 5_400_000: -5_397_000 / 62 = -87_048.
        exp = xp_to_level(60)
        self.assertEqual(death_loss(exp, 60, full=False), -8_704)  # -87_048 / 10 -> -8_704
        self.assertEqual(death_loss(exp, 60, full=True), -8_704 - 87_048)  # tenth plus full


class KillShareLevelNinetyAndThirtyOnALevelFifteenMob(unittest.TestCase):
    """Reproduces src/tests/xp_formula_tests.cpp KillModifiersForALevelNinetyAndALevelThirtyKillerOnALevelFifteenMob."""

    def setUp(self):
        self.mob = _mob(level=15, exp=3930)

    def test_kill_share_base_for_killer_ninety(self):
        # levelb(90) = min(90, 20 + 90/3) = 50; level_total = 2*50 = 100 (solo kill).
        # share = (3930/10) * 2 / 1 / 100 = 393 * 2 / 100 = 786/100 = 7; base = 7*50 = 350.
        self.assertEqual(kill_share([90], self.mob, attacked_level=50), [350])

    def test_kill_share_base_for_killer_thirty(self):
        # levelb(30) = min(30, 20 + 30/3) = 30; level_total = 60.
        # share = 393 * 2 / 1 / 60 = 786/60 = 13; base = 13*30 = 390.
        self.assertEqual(kill_share([30], self.mob, attacked_level=30), [390])

    def test_exp_with_modifiers_killer_ninety_crushed_by_level_gap(self):
        # step 2: base /= max(91, 13) = 91 -> 350/91 = 3.
        # step 4: 15+6=21 < 90, base = 6*3/(90-15) = 18/75 = 0; everything after stays 0.
        self.assertEqual(
            exp_with_modifiers(killer_level=90, killer_is_good_race=True, killer_is_good_align=True,
                                killer_is_orc=False, mob=self.mob, zone_x=8, difficulty=0,
                                age_ticks=40, average_mob_life=40, base_exp=350),
            0,
        )

    def test_exp_with_modifiers_killer_thirty(self):
        # step 2: base /= max(31, 13) = 31 -> 390/31 = 12.
        # step 4: 15+6=21 < 30, base = 6*12/(30-15) = 72/15 = 4; exp = 4.
        # step 5 (age): derived age = 40*40/(15+20) = 1600/35 = 45 >= 40 (average_mob_life), so
        #   exp = 4 * (140 - 40*40/45) / 100 = 4 * (140-35)/100 = 4*105/100 = 420/100 = 4.
        # steps 6-9: no flags, mob not good, difficulty 0, zone x=8 (no east bonus), TEMPORARY
        #   adds 2*4/29 = 8/29 = 0.
        self.assertEqual(
            exp_with_modifiers(killer_level=30, killer_is_good_race=True, killer_is_good_align=True,
                                killer_is_orc=False, mob=self.mob, zone_x=8, difficulty=0,
                                age_ticks=40, average_mob_life=40, base_exp=390),
            4,
        )


class KillModifiersForALevelMatchedLevelSixtyKill(unittest.TestCase):
    def setUp(self):
        self.mob = _mob(level=60, exp=3930)

    def test_kill_share_base(self):
        # levelb(60) = min(60, 20+20) = 40; level_total = 80.
        # share = 393*2/1/80 = 786/80 = 9; base = 9*40 = 360.
        self.assertEqual(kill_share([60], self.mob, attacked_level=40), [360])

    def test_exp_with_modifiers(self):
        # step 2: base /= max(61, 58) = 61 -> 360/61 = 5.
        # step 4: mob level+6=66 < killer level 60? no -> base stays 5; exp = 5.
        # step 5 (age): derived age = 40*40/(60+20) = 1600/80 = 20 < 40 (average_mob_life), so
        #   exp = 5 * (40*60 + 20*40) / (40*100) = 5*3200/4000 = 16000/4000 = 4.
        # steps 6-9: no flags, mob not good, difficulty 0, zone x=8, TEMPORARY adds 8/59 = 0.
        self.assertEqual(
            exp_with_modifiers(killer_level=60, killer_is_good_race=True, killer_is_good_align=True,
                                killer_is_orc=False, mob=self.mob, zone_x=8, difficulty=0,
                                age_ticks=40, average_mob_life=40, base_exp=360),
            4,
        )


class EastOfTheRiverAddsUpToFifteenPercentForGoodRaces(unittest.TestCase):
    """Reproduces src/tests/xp_formula_tests.cpp EastOfTheRiverAddsUpToFifteenPercentForGoodRaces
    as revised at commit b882123 (the level-30 fixture the test used to rely on cannot distinguish
    x = 8 from x = 13, since 4 * 15 / 100 truncates to 0 either way; the level-20 fixture below is
    the current distinguishing pair)."""

    def test_level_twenty_killer_shows_the_bonus_surviving_truncation(self):
        mob = _mob(level=15, exp=3930)

        # kill_share([20], mob, attacked_level=levelb(20)=20): levelb(20)=min(20,20+20/3)=20;
        # level_total=40; share=393*2/1/40=786/40=19; base=19*20=380.
        # step 2: base /= max(21, 13) = 21 -> 380/21 = 18.
        # step 4: 15+6=21 < 20? no -- base stays 18; exp = 18.
        # step 5 (age): derived age = 40*40/(15+20) = 1600/35 = 45 >= 40, so
        #   exp = 18 * (140 - 40*40/45) / 100 = 18*(140-35)/100 = 18*105/100 = 1890/100 = 18.
        # steps 6-7: no change. step 8 (x=8): not > 8, no change.
        # step 9 (TEMPORARY): exp += 2*18/max(1,19) = 36/19 = 1 -> exp = 19.
        baseline = exp_with_modifiers(killer_level=20, killer_is_good_race=True,
                                       killer_is_good_align=True, killer_is_orc=False, mob=mob,
                                       zone_x=8, difficulty=0, age_ticks=40, average_mob_life=40,
                                       base_exp=380)
        self.assertEqual(baseline, 19)

        # step 8 (x=13): exp += 18 * min(13-8,5)*3/100 = 18*15/100 = 270/100 = 2 -> exp = 20.
        # step 9 (TEMPORARY): exp += 2*20/max(1,19) = 40/19 = 2 -> exp = 22.
        with_east_bonus = exp_with_modifiers(killer_level=20, killer_is_good_race=True,
                                              killer_is_good_align=True, killer_is_orc=False, mob=mob,
                                              zone_x=13, difficulty=0, age_ticks=40,
                                              average_mob_life=40, base_exp=380)
        self.assertEqual(with_east_bonus, 22)

    def test_level_ninety_killer_stays_zero_at_both_zone_values(self):
        # Contrasting pair: base is already crushed to 0 by the level-gap divide (step 4) before
        # the east bonus is ever reached, so x = 8 and x = 13 are identical here for a different
        # reason than the level-20 case (zero input, not a rounded-away percentage).
        mob = _mob(level=15, exp=3930)

        baseline = exp_with_modifiers(killer_level=90, killer_is_good_race=True,
                                       killer_is_good_align=True, killer_is_orc=False, mob=mob,
                                       zone_x=8, difficulty=0, age_ticks=40, average_mob_life=40,
                                       base_exp=350)
        self.assertEqual(baseline, 0)

        with_east_bonus = exp_with_modifiers(killer_level=90, killer_is_good_race=True,
                                              killer_is_good_align=True, killer_is_orc=False, mob=mob,
                                              zone_x=13, difficulty=0, age_ticks=40,
                                              average_mob_life=40, base_exp=350)
        self.assertEqual(with_east_bonus, 0)


class GoodKillingGoodTakesTwoThirds(unittest.TestCase):
    """Reproduces src/tests/xp_formula_tests.cpp GoodKillingGoodTakesTwoThirds."""

    def test_good_on_good_penalty(self):
        neutral_mob = _mob(level=15, exp=3930, alignment=0)
        good_mob = _mob(level=15, exp=3930, alignment=1000)

        neutral_result = exp_with_modifiers(killer_level=30, killer_is_good_race=True,
                                             killer_is_good_align=True, killer_is_orc=False,
                                             mob=neutral_mob, zone_x=8, difficulty=0, age_ticks=40,
                                             average_mob_life=40, base_exp=390)
        self.assertEqual(neutral_result, 4)

        # step 6: IS_GOOD(killer) && IS_GOOD(mob) -> exp = 4*2/3 = 8/3 = 2.
        good_on_good_result = exp_with_modifiers(killer_level=30, killer_is_good_race=True,
                                                  killer_is_good_align=True, killer_is_orc=False,
                                                  mob=good_mob, zone_x=8, difficulty=0, age_ticks=40,
                                                  average_mob_life=40, base_exp=390)
        self.assertEqual(good_on_good_result, 2)


class MobSpecBonusGate(unittest.TestCase):
    """The MOB_SPEC bonus (src/fight.cpp:1378) gates on MOB_FLAGGED(MOB_SPEC) AND (mob_index[nr].
    func OR store_prog_number). This module has no visibility into mob_index[].func (a hardcoded
    C function pointer assigned by vnum at boot, src/spec_ass.cpp's ASSIGNMOB calls -- not data in
    the mob file), so it models the store_prog_number half only, via MobRecord.prog != 0. Same
    level-20-killer / level-15-mob fixture (base 380) as the east-bonus reconciliation above,
    isolating the MOB_SPEC step: mob alignment 0 (no good-on-good), default_pos standing (no
    below-standing penalty), zone x = 8 (no east bonus), no other mob_flags bits set."""

    def test_mob_spec_with_no_prog_number_gets_no_bonus(self):
        from parse_mobs import MOB_SPEC
        mob = _mob(level=15, exp=3930, mob_flags=MOB_SPEC)
        self.assertEqual(mob.prog, 0)

        # Same derivation through step 5 as the level-20 east-bonus baseline: exp = 18 going into
        # the MOB_SPEC step. prog == 0, so mob_index[nr].func is the only way in (invisible here)
        # -- no +base_exp/10 is applied; exp stays 18 through difficulty/east bonus, then
        # TEMPORARY: exp += 2*18/19 = 36/19 = 1 -> exp = 19.
        self.assertEqual(
            exp_with_modifiers(killer_level=20, killer_is_good_race=True, killer_is_good_align=True,
                                killer_is_orc=False, mob=mob, zone_x=8, difficulty=0, age_ticks=40,
                                average_mob_life=40, base_exp=380),
            19,
        )

    def test_mob_spec_with_a_prog_number_gets_the_bonus(self):
        from parse_mobs import MOB_SPEC
        mob = _mob(level=15, exp=3930, mob_flags=MOB_SPEC, prog=9001)

        # exp = 18 going into the MOB_SPEC step (identical to the no-bonus case above). prog != 0,
        # so exp += base_exp/10 = 18/10 = 1 -> exp = 19; TEMPORARY: exp += 2*19/19 = 38/19 = 2
        # -> exp = 21.
        self.assertEqual(
            exp_with_modifiers(killer_level=20, killer_is_good_race=True, killer_is_good_align=True,
                                killer_is_orc=False, mob=mob, zone_x=8, difficulty=0, age_ticks=40,
                                average_mob_life=40, base_exp=380),
            21,
        )


class SoloKillXpComposesShareAndModifiers(unittest.TestCase):
    def test_solo_kill_at_level_thirty_against_the_pinned_level_fifteen_mob(self):
        # attacked_level = levelb(30) = 30; kill_share([30], mob, 30) = [390] (see above);
        # exp_with_modifiers(30, ..., base_exp=390) = 4; gain_exp_clamp(4, level=30) = 4.
        mob = _mob(level=15, exp=3930)
        self.assertEqual(
            solo_kill_xp(30, mob, zone_x=8, killer_is_good_race=True, killer_is_good_align=True,
                         killer_is_orc=False),
            4,
        )

    def test_a_level_ninety_killer_earns_zero_from_any_kill(self):
        # A level-90 killer's exp_with_modifiers output is nonzero (the level-gap divisor only
        # zeroes a killer far ABOVE the victim, and here killer == mob level), but gain_exp_clamp
        # forces every positive gain to 0 at level >= 90 (src/limits.cpp:416) -- the real research
        # result finding 2 calls out.
        mob = _mob(level=90, exp=12_150_000)
        self.assertEqual(
            solo_kill_xp(90, mob, zone_x=8, killer_is_good_race=True, killer_is_good_align=True,
                         killer_is_orc=False),
            0,
        )


class OrcKillerGetsNothingForAnOrcFriend(unittest.TestCase):
    def test_step_one_short_circuits_to_zero(self):
        from parse_mobs import MOB_ORC_FRIEND
        mob = _mob(level=15, exp=3930, mob_flags=MOB_ORC_FRIEND)
        self.assertEqual(
            exp_with_modifiers(killer_level=30, killer_is_good_race=False, killer_is_good_align=False,
                                killer_is_orc=True, mob=mob, zone_x=8, difficulty=0, age_ticks=40,
                                average_mob_life=40, base_exp=390),
            0,
        )


if __name__ == "__main__":
    unittest.main()
