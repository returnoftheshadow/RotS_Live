# Hand-verification (Step 7), vnum -> (level, exp, max_hit, mob_flags) checked
# by eye against the source .mob file and the CSV row the parser produced for
# it on 2026-09-19. 200.mob, 300.mob and the last populated file (341.mob;
# 342-349.mob are terminator-only, zero mobs) turned out to hold unused
# all-zero "golem" template records rather than active mobs; those are
# genuine world data (raw file confirmed by eye), not a parser defect, and
# the last file was chosen as the last one with any record rather than the
# last file by name:
#   10000 (100.mob, zone 100, 1st record): level 8, exp 3930, max_hit 114, flags 526347 -- match
#   15000 (150.mob, zone 150, 1st record): level 2, exp 3300, max_hit 30,  flags 524393 -- match
#   20073 (200.mob, zone 200, only record): level 0, exp 0,   max_hit 0,   flags 8 -- match
#   30000 (300.mob, zone 300, 1st record):  level 0, exp 100, max_hit 10,  flags 8 -- match
#   34100 (341.mob, zone 341, last file with any record): level 0, exp 0, max_hit 0, flags 8 -- match
import unittest
from pathlib import Path

import parse_mobs
from parse_mobs import parse_mob_file
from parse_zones import parse_zone_file

WORLD = Path(__file__).resolve().parents[2] / "lib" / "world"


class ParseMobFile(unittest.TestCase):
    def test_first_record_of_zone_100_matches_the_loader_field_order(self):
        records = parse_mob_file(WORLD / "mob" / "100.mob")
        tailor = records[0]
        self.assertEqual(tailor.vnum, 10000)
        self.assertEqual(tailor.short_descr, "Medrel Inding")
        self.assertEqual(tailor.mob_flags, 526347)
        self.assertEqual(tailor.alignment, 0)
        self.assertEqual((tailor.level, tailor.ob, tailor.parry, tailor.dodge), (8, 32, 16, 8))
        self.assertEqual((tailor.hit, tailor.max_hit), (77, 114))
        self.assertEqual((tailor.damage, tailor.ene_regen), (5, 86))
        self.assertEqual((tailor.gold, tailor.exp), (3000, 3930))
        self.assertEqual((tailor.position, tailor.default_pos, tailor.sex, tailor.race), (8, 8, 1, 1))
        self.assertEqual(tailor.spirit, 0)

    def test_every_world_file_parses_and_every_record_has_a_non_negative_level(self):
        # lib/world/mob/10079 ("golem") is an unused, all-zero-stat template
        # record; its level 0 is genuine data, not a parser bug, so the bound
        # is >= 0, not the stricter > 0 that would reject a legitimate level-0
        # template.
        total = 0
        for path in sorted((WORLD / "mob").glob("*.mob")):
            if not path.stem.isdigit():
                continue
            for record in parse_mob_file(path):
                total += 1
                self.assertGreaterEqual(record.level, 0, f"vnum {record.vnum} in {path.name}")
        self.assertGreater(total, 3000)

    def test_only_the_trailing_will_teach_field_ever_defaults(self):
        # fscanf("%d") silently defaults a missing trailing integer to 0
        # (parse_mobs._Cursor.read_ints); 2441 of 3723 world mob records omit
        # only the final will_teach field before the next record marker. This
        # wraps read_ints, over the real parse of the whole corpus, to record
        # every (call_size, index) pair where a default fired, then asserts
        # the only position ever defaulted is index 6 of a 7-value call -- the
        # will_teach slot -- so a default anywhere else, which would mean the
        # field order has drifted out of alignment with db.cpp, fails this
        # test instead of silently returning 0.
        defaulted_positions: list[tuple[int, int]] = []
        original_read_ints = parse_mobs._Cursor.read_ints

        def traced_read_ints(self, count):
            values = []
            for index in range(count):
                checkpoint = self.pos
                token = self.read_token()
                try:
                    values.append(int(token))
                except ValueError:
                    self.pos = checkpoint
                    values.append(0)
                    defaulted_positions.append((count, index))
            return values

        parse_mobs._Cursor.read_ints = traced_read_ints
        try:
            records = parse_mobs.parse_all(WORLD / "mob")
        finally:
            parse_mobs._Cursor.read_ints = original_read_ints

        self.assertGreater(len(records), 3000)
        self.assertTrue(defaulted_positions, "expected at least one defaulted field in the corpus")
        self.assertTrue(
            all(position == (7, 6) for position in defaulted_positions),
            "a field other than the trailing will_teach integer defaulted to 0 -- "
            "this indicates field misalignment in the parser",
        )


class ParseZoneFile(unittest.TestCase):
    def test_zone_100_header_exposes_the_real_map_coordinates(self):
        zone = parse_zone_file(WORLD / "zon" / "100.zon")
        self.assertEqual(zone.number, 100)
        self.assertEqual((zone.symbol, zone.x, zone.y, zone.level), ("L", 24, 3, 0))
        self.assertEqual(zone.top, 10099)
        self.assertEqual((zone.lifespan, zone.reset_mode), (10, 2))

    def test_zone_100_is_east_of_the_river_with_the_capped_bonus(self):
        zone = parse_zone_file(WORLD / "zon" / "100.zon")
        self.assertTrue(zone.is_east_of_river)
        self.assertEqual(zone.east_bonus_percent, 15)

    def test_zone_100_mob_loads_include_the_tailor_at_the_documented_difficulty(self):
        zone = parse_zone_file(WORLD / "zon" / "100.zon")
        tailor_loads = [load for load in zone.mob_loads if load.vnum == 10000]
        self.assertEqual(len(tailor_loads), 1)
        self.assertEqual((tailor_loads[0].room, tailor_loads[0].difficulty), (10088, 100))

    def test_zone_231_maze_map_field_does_not_truncate_the_header(self):
        # 231.zon writes 'Old maze level 1~~' as its name line (an empty
        # description) followed by a multi-line ASCII maze as the map field,
        # which only a fread_string-accurate, line-based reader parses past
        # without landing on the maze art as if it were the owner list.
        zone = parse_zone_file(WORLD / "zon" / "231.zon")
        self.assertEqual((zone.x, zone.y), (11, 10))
        self.assertTrue(zone.mob_loads)


if __name__ == "__main__":
    unittest.main()
