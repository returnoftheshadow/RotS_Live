#!/usr/bin/env python3
"""Unit tests for the research tools; they use small inline fixtures, never the real repository.

    python3 tools/research/research_tests.py
"""

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Dict, List, Optional

RESEARCH_DIR = Path(__file__).resolve().parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(RESEARCH_DIR))
import class_packs  # noqa: E402
import command_table  # noqa: E402
import handler_gates  # noqa: E402
import mob_census  # noqa: E402
import practice_curve  # noqa: E402
import rots_sources  # noqa: E402
import skill_data  # noqa: E402
import trainer_tables  # noqa: E402
import wait_calls  # noqa: E402

FILLER_ROW = '    { "skill %d", PROF_MAGE, 1, spell_filler, POSITION_STANDING, 5, 12, 8, 10, 1, 0, PLRSPEC_FIRE, RESIST_NONE },'
TRAP_ROW_INDEX = 151


def skills_fixture() -> str:
    """A skills[] table of 154 rows: two warrior rows (one wrapped, one after a comment), mage filler, the
    unnamed trap row 151, stomp (general, documented as warrior) and an internal row."""
    rows = ["struct skill_data skills[MAX_SKILLS] = {",
            "    /*   0  Warrior skills */",
            '    { "barehanded", PROF_WARRIOR, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 25, 1, 0, PLRSPEC_NONE, RESIST_NONE },',
            '    { "slashing", PROF_WARRIOR, 0, NULL, POSITION_FIGHTING, 0, 0, 16, 30, 1, 0,',
            "        PLRSPEC_NONE, RESIST_NONE }, // wrapped row"]
    rows += [FILLER_ROW % row_index for row_index in range(2, TRAP_ROW_INDEX)]
    rows += ['    { "", PROF_GENERAL, 0, NULL, POSITION_STANDING, 0, 0, 16, 20, 1, 0, PLRSPEC_NONE, RESIST_NONE },',
             '    { "stomp", PROF_GENERAL, 5, NULL, POSITION_FIGHTING, 0, 0, 16, 15, 1, 0, PLRSPEC_HFGT, RESIST_NONE },',
             '    { "nothing", PROF_GENERAL, 0, NULL, POSITION_DEAD, 0, 0, 16, 0, 1, 0, PLRSPEC_NONE, RESIST_NONE },',
             "};"]
    return "\n".join(rows)


def guilds_fixture() -> str:
    """guildmasters[] with three entries: a // label, a /* */ label teaching the trap row, and no label. Every
    array is shorter than MAX_SKILLS."""
    trap_caps = ["0"] * TRAP_ROW_INDEX + ["80", "70"]
    return "\n".join([
        "struct skill_teach_data guildmasters[] = {",
        "    { // WARRIOR GUILD (1)",
        '        "Hello", "Sorry", "$n teaches", "You practice", "bows", "perfect",',
        "        { 0, 100, /*2*/ 50 } },",
        "    { /* TRAPPERS (2) */",
        '        "Hi", "No", "$n", "You", "bow", "done",',
        "        { " + ", ".join(trap_caps) + " } },",
        "    {",
        '        "Unlabelled greeting", "b", "c", "d", "e", "f",',
        "        { 0 } },",
        "};"])


def mob_record(vnum: int, act_flags: int, numbers: List[int], letter: str = "N") -> str:
    """One .mob record; N records carry two death-cry strings."""
    death_cries = "It dies.~\nYou hear a cry.~\n" if letter == "N" else ""
    rows = [numbers[index:index + 8] for index in range(0, len(numbers), 8)]
    return (f"#{vnum}\nkeywords~\nmob {vnum}~\nA mob stands here.~\nIt looks\n\rlike a mob.\n\r~\n"
            f"{act_flags} 0 0 {letter}\n{death_cries}" + "\n".join(" ".join(map(str, row)) for row in rows) + "\n")


def mob_numbers(level: int, race: int, program: int, will_teach: Optional[int]) -> List[int]:
    """The 40 record integers (39 when will_teach is None, as when a record stops early)."""
    numbers = [0] * 40
    numbers[rots_sources.MOB_FIELDS.index("level")] = level
    numbers[rots_sources.MOB_FIELDS.index("race")] = race
    numbers[rots_sources.MOB_FIELDS.index("prog")] = program
    if will_teach is None:
        return numbers[:39]
    numbers[rots_sources.MOB_FIELDS.index("will_teach")] = will_teach
    return numbers


MOB_FILE = (mob_record(100, 1, mob_numbers(20, 1, 1, 63))
            + mob_record(101, 3, mob_numbers(15, 13, 2, None), letter="M")
            + mob_record(102, 0, mob_numbers(20, 1, 9, 63))
            + "#99999\n$~\n")
ROOM_FILE = ("#1000 \n\rA Training Yard~\n\r   Dust everywhere.\n\r~\n\r10 0 0\n\rS\n\r"
             "#1001\n\rA Dark Hut~\n\r   Dark.\n\r~\n\r10 0 0\n\rS\n\r$~\n\r")
ZONE_FILE = ("#10 Test Zone~\nA zone for tests.\n~\n"
             "M 0 100 1000 0 100 100 1 0 load the warrior trainer\n"
             "O 0 5 1000 0 100\n"
             "M 1 101 1001 0 100 100 1 0 load the trapper\n"
             "S\n")
SPEC_ASS_FILE = ("    ASSIGNMOB(100, guild); // warrior trainer\n"
                 "    ASSIGNMOB(101, guild); // trapper -- will_teach 0\n"
                 "    ASSIGNMOB(102, guild);\n"
                 "    // ASSIGNMOB(103, guild); retired\n"
                 "    ASSIGNMOB(104, guild); // missing mob\n"
                 "    ASSIGNMOB(105, cityguard);\n")
SPELLS_HEADER = ("#define SKILL_BAREHANDED 0\n#define SKILL_SLASH 1\n"
                 "/* reserved\n#define LANG_ANIMAL 121\n*/\n#define SKILL_TRAP 151\n")
STRUCTS_HEADER = "#define LANG_ANIMAL 121\n#define MOB_SPEC (1 << 0) /* spec-proc */\n"


def write_fake_repo(root: Path) -> None:
    """A repository holding only the files the class tools read."""
    files: Dict[str, str] = {
        "src/consts.cpp": "#include \"structs.h\"\n\n" + skills_fixture() + "\n\n" + guilds_fixture() + "\n",
        "src/spells.h": SPELLS_HEADER, "src/structs.h": STRUCTS_HEADER, "src/spec_ass.cpp": SPEC_ASS_FILE,
        "lib/world/mob/1.mob": MOB_FILE, "lib/world/wld/10.wld": ROOM_FILE, "lib/world/zon/10.zon": ZONE_FILE}
    for relative_path, text in files.items():
        (root / relative_path).parent.mkdir(parents=True, exist_ok=True)
        (root / relative_path).write_text(text, encoding="latin-1")


class TempDirTestCase(unittest.TestCase):
    """Gives each test a fresh temporary directory that is removed afterwards."""

    def setUp(self) -> None:
        temp_dir = tempfile.TemporaryDirectory()
        self.addCleanup(temp_dir.cleanup)
        self.root = Path(temp_dir.name).resolve()


class CSourceHelpersTest(unittest.TestCase):
    def test_comments_are_blanked_keeping_offsets_and_strings(self) -> None:
        text = 'a = 1; // note\nb = "http://x"; /* two\nlines */ c = \'/\';'

        blanked = rots_sources.blank_c_comments(text)

        self.assertEqual(len(blanked), len(text))
        self.assertEqual(blanked.count("\n"), text.count("\n"))
        self.assertNotIn("note", blanked)
        self.assertNotIn("lines", blanked)
        self.assertIn('"http://x"', blanked)
        self.assertIn("c = '/';", blanked)

    def test_arguments_split_only_at_top_level_commas(self) -> None:
        arguments = rots_sources.split_arguments(' ch, number(2,\n  4), list[a, b], "x, y", f(g(1, 2)) ')

        self.assertEqual(arguments, ["ch", "number(2, 4)", "list[a, b]", '"x, y"', "f(g(1, 2))"])

    def test_c_division_truncates_toward_zero(self) -> None:
        self.assertEqual(rots_sources.c_divide(7, 2), 3)
        self.assertEqual(rots_sources.c_divide(-7, 2), -3)
        self.assertEqual(rots_sources.c_divide(7, -2), -3)
        self.assertEqual(rots_sources.c_divide(-7, -2), 3)

    def test_function_end_is_the_first_closing_brace_line(self) -> None:
        lines = ["ACMD(do_hide)", "{", "    if (x) {", "    }", "}", "ACMD(do_sneak)"]

        self.assertEqual(rots_sources.function_end(lines, 0), 4)

    def test_handler_declarations_are_not_definitions(self) -> None:
        lines = ["ACMD(do_hide);", "void do_hide(char_data* ch, char* argument);", "ACMD(do_hide)", "{", "}",
                 "void do_hide(char_data* ch, char* argument, waiting_type* wait_list, int cmd, int subcmd)"]

        self.assertEqual(rots_sources.find_handler_definitions(lines, "hide"), [2, 5])


class SkillsTableTest(unittest.TestCase):
    def test_rows_keep_their_order_fields_and_source_lines(self) -> None:
        text = "// header\n" + skills_fixture()

        skills = rots_sources.parse_skills(text)

        self.assertEqual(len(skills), 154)
        self.assertEqual(skills[0]["name"], "barehanded")
        self.assertEqual(skills[0]["source_line"], 4)
        self.assertEqual(skills[1]["source_line"], 5)
        self.assertEqual((skills[1]["type"], skills[1]["learn_diff"], skills[1]["skill_spec"]), ("warrior", 30, "nothing"))
        self.assertEqual(skills[2]["skill_spec"], "fire")
        self.assertEqual((skills[TRAP_ROW_INDEX]["name"], skills[TRAP_ROW_INDEX]["type"]), ("", "general"))

    def test_row_with_a_missing_field_is_refused(self) -> None:
        text = 'struct skill_data skills[MAX_SKILLS] = {\n    { "short", PROF_MAGE, 1 },\n};'

        with self.assertRaises(ValueError):
            rots_sources.parse_skills(text)

    def test_commented_out_defines_are_ignored(self) -> None:
        defines = rots_sources.parse_skill_defines([SPELLS_HEADER, STRUCTS_HEADER])

        self.assertEqual(defines[121], ["LANG_ANIMAL"])
        self.assertEqual(defines[151], ["SKILL_TRAP"])


class GuildTableTest(unittest.TestCase):
    def test_guilds_are_numbered_from_one_with_their_labels(self) -> None:
        guilds = rots_sources.parse_guilds("int x;\n" + guilds_fixture())

        self.assertEqual([guild["number"] for guild in guilds], [1, 2, 3])
        self.assertEqual([guild["label"] for guild in guilds],
                         ["WARRIOR GUILD (1)", "TRAPPERS (2)", "(no label) Unlabelled greeting"])
        self.assertEqual(guilds[0]["knowledge"], [0, 100, 50])
        self.assertEqual(guilds[0]["source_line"], 3)

    def test_short_array_means_zero_for_missing_skills(self) -> None:
        guild = rots_sources.parse_guilds(guilds_fixture())[0]

        self.assertEqual(rots_sources.guild_cap(guild, 2), 50)
        self.assertEqual(rots_sources.guild_cap(guild, 200), 0)

    def test_guildmaster_vnums_come_from_live_assignmob_lines(self) -> None:
        entries = rots_sources.parse_guildmaster_vnums(SPEC_ASS_FILE)

        self.assertEqual([entry["vnum"] for entry in entries], [100, 101, 102, 104])
        self.assertEqual(entries[1]["comment"], "trapper -- will_teach 0")
        self.assertEqual(entries[2]["comment"], "")
        self.assertEqual(entries[3]["spec_ass_line"], 5)


class WorldFilesTest(unittest.TestCase):
    def test_mob_records_read_their_numbers_and_flags(self) -> None:
        mobs = rots_sources.parse_mob_file(MOB_FILE, "1.mob", spec_shift=0)

        self.assertEqual(sorted(mobs), [100, 101, 102])
        self.assertEqual((mobs[100]["level"], mobs[100]["prog"], mobs[100]["will_teach"]), (20, 1, 63))
        self.assertEqual(mobs[100]["short_desc"], "mob 100")
        self.assertTrue(mobs[100]["spec_flag"])
        self.assertFalse(mobs[102]["spec_flag"])

    def test_record_that_stops_early_teaches_nobody(self) -> None:
        mob = rots_sources.parse_mob_file(MOB_FILE, "1.mob")[101]

        self.assertEqual(mob["token_count"], 39)
        self.assertEqual(mob["will_teach"], 0)
        self.assertEqual((mob["level"], mob["race"], mob["prog"]), (15, 13, 2))

    def test_wanted_limits_the_records(self) -> None:
        self.assertEqual(list(rots_sources.parse_mob_file(MOB_FILE, "1.mob", wanted={101})), [101])

    def test_room_names_handle_newline_carriage_return(self) -> None:
        self.assertEqual(rots_sources.parse_room_names(ROOM_FILE), {1000: "A Training Yard", 1001: "A Dark Hut"})

    def test_zone_m_lines_load_mobs_into_rooms(self) -> None:
        zone_name, loads = rots_sources.parse_zone(ZONE_FILE, "10.zon")

        self.assertEqual(zone_name, "Test Zone")
        self.assertEqual([(load["mob"], load["room"]) for load in loads], [(100, 1000), (101, 1001)])

    def test_race_mask_names_races_by_bit(self) -> None:
        self.assertEqual(rots_sources.race_names_in_mask(63), ["God", "Human", "Dwarf", "Wood Elf", "Hobbit", "High Elf"])
        self.assertEqual(rots_sources.race_names_in_mask(0), [])


class ClassToolsTest(TempDirTestCase):
    def setUp(self) -> None:
        super().setUp()
        write_fake_repo(self.root)
        self.data = skill_data.build_skill_data(self.root)

    def guildmaster(self, vnum: int) -> Dict:
        return next(entry for entry in self.data["guildmasters"] if entry["vnum"] == vnum)

    def test_guildmasters_are_joined_with_mobs_guilds_and_rooms(self) -> None:
        trainer = self.guildmaster(100)

        self.assertEqual((trainer["guild"], trainer["guild_label"], trainer["race_name"]),
                         (1, "WARRIOR GUILD (1)", "Human"))
        self.assertEqual(trainer["loads"], [{"room": 1000, "zone_file": "10.zon", "zone": "Test Zone",
                                             "loaded_by": "Test Zone", "room_name": "A Training Yard"}])
        self.assertEqual(self.guildmaster(102)["guild_label"], "OUT OF RANGE")
        self.assertFalse(self.guildmaster(104)["found_in_world"])
        self.assertEqual(self.data["mob_spec_shift"], 0)

    def test_trainer_problems_name_every_reason(self) -> None:
        guild_count = len(self.data["guilds"])

        self.assertEqual(class_packs.trainer_problems(self.guildmaster(100), guild_count), [])
        self.assertEqual(class_packs.trainer_problems(self.guildmaster(101), guild_count, brief=True),
                         ["teaches no race (will_teach 0)"])
        self.assertEqual(class_packs.trainer_problems(self.guildmaster(102), guild_count, brief=True),
                         ["no SPEC flag", "program number is not a guild", "never loaded by a zone reset"])
        self.assertEqual(len(class_packs.trainer_problems(self.guildmaster(104), guild_count)), 5)

    def test_packs_assign_skills_to_classes(self) -> None:
        packs = class_packs.build_packs(self.data)

        self.assertEqual([skill["name"] for skill in packs["warrior"]], ["barehanded", "slashing", "stomp"])
        self.assertEqual([skill["name"] for skill in packs["ranger"]], [class_packs.TRAP_ROW_NAME])
        self.assertEqual(len(packs["mage"]), 149)
        self.assertEqual(packs["general"], [])

    def test_pack_lists_trainers_with_caps_and_problems(self) -> None:
        packs = class_packs.build_packs(self.data)

        warrior_text = class_packs.format_pack("warrior", packs["warrior"])
        ranger_text = class_packs.format_pack("ranger", packs["ranger"])

        self.assertIn("# Data pack: warrior (3 skills)", warrior_text)
        self.assertIn("- trainers: NONE (no guild row teaches it)", warrior_text)
        self.assertIn("- 100%: mob 100 (mob 100, Human), guild 1 'WARRIOR GUILD (1)', teaches ['God', 'Human', "
                      "'Dwarf', 'Wood Elf', 'Hobbit', 'High Elf'], loads in room 1000 'A Training Yard' "
                      "(Test Zone, 10.zon)", warrior_text)
        self.assertIn("## 151 trap (row 151 has an empty name)  [SKILL_TRAP]", ranger_text)
        self.assertIn("- 80%: mob 101 (mob 101, Orc), guild 2 'TRAPPERS (2)', teaches [], loads in room 1001 "
                      "'A Dark Hut' (Test Zone, 10.zon); PROBLEMS: will_teach race mask is 0, so it teaches nobody",
                      ranger_text)

    def test_warrior_table_summarises_weapons_and_lists_unusable(self) -> None:
        table = trainer_tables.build_table("warrior", self.data)

        self.assertIn("| mob 100 (mob 100) | A Training Yard (room 1000), Test Zone | Light races | "
                      "1 of 10, up to 100 | none |", table)
        self.assertIn("UNUSABLE: mob 102 (mob 102): no SPEC flag; program number is not a guild; "
                      "never loaded by a zone reset", table)
        # Guild 2 teaches stomp, a general skill documented under warrior.
        self.assertIn("UNUSABLE: mob 101 (mob 101): teaches no race (will_teach 0)", table)

    def test_ranger_table_reports_the_trapper_unusable(self) -> None:
        table = trainer_tables.build_table("ranger", self.data)

        self.assertIn("UNUSABLE: mob 101 (mob 101): teaches no race (will_teach 0)", table)

    def test_clis_write_json_and_packs_from_either_source(self) -> None:
        output = self.root / "out"
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(skill_data.main(["--repo", str(self.root), "--out", str(output), "--summary"]), 0)
            self.assertEqual(class_packs.main(["--repo", str(self.root), "--out", str(output / "direct")]), 0)
            self.assertEqual(class_packs.main(["--data", str(output / "skill_data.json"),
                                               "--out", str(output / "json")]), 0)

        self.assertIn("skills rows: 154; guild rows: 3; guildmaster vnums: 4",
                      (output / "skill_data.txt").read_text(encoding="utf-8"))
        for class_name in class_packs.CLASS_NAMES:
            self.assertEqual((output / "direct" / f"pack_{class_name}.md").read_text(encoding="utf-8"),
                             (output / "json" / f"pack_{class_name}.md").read_text(encoding="utf-8"))
        self.assertEqual(len(json.loads((output / "json" / "pack_warrior.json").read_text(encoding="utf-8"))), 3)


INTERPRETER_FIXTURE = """#define COMMANDO(number, min_pos, pointer, min_level, \\
    retired, subcommand, targfl1, targfl2, special_mask) \\
    { cmd_info[(number)].command_pointer = (pointer); }

const char* command[] = {
    "north", /* 1 */
    "east",
    "",
    "look", // 4
    "defend",
    "\\n"
};

void assign_command_pointers(void)
{
    int position;

    COMMANDO(1, POSITION_STANDING, do_move, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    COMMANDO(2, POSITION_STANDING, do_move, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
    // COMMANDO(3, POSITION_DEAD, do_nothing, 0, TRUE, 0, 0, 0, 0);
    COMMANDO(4, POSITION_RESTING, do_look, LEVEL_GOD + 1, FALSE, SCMD_LOOK,
        TAR_CHAR_ROOM | TAR_OBJ_ROOM, FULL_TARGET, CMD_MASK_NO_UNHIDE);
    COMMANDO(CMD_DEFEND, POSITION_FIGHTING, do_defend, 0, TRUE, 0,
        FULL_TARGET, FULL_TARGET, 0);
}

void other(void)
{
}
"""


class LoadZoneTest(TempDirTestCase):
    """A zone can load a mob into a room that belongs to another zone."""

    def setUp(self) -> None:
        super().setUp()
        loader_zone = "#10 Loader Zone~\nLoads elsewhere.\n~\nM 0 7 1100 0 100 100 1 0 load into zone 11\nS\n"
        files = {"lib/world/wld/10.wld": ROOM_FILE,
                 "lib/world/wld/11.wld": "#1100\n\rA Far Room~\n\r   Far.\n\r~\n\r11 0 0\n\rS\n\r$~\n\r",
                 "lib/world/zon/10.zon": loader_zone,
                 "lib/world/zon/11.zon": "#11 Room Zone~\nHolds the room.\n~\nS\n"}
        for relative_path, text in files.items():
            (self.root / relative_path).parent.mkdir(parents=True, exist_ok=True)
            (self.root / relative_path).write_text(text, encoding="latin-1")

    def test_load_names_the_rooms_zone_and_the_loading_zone(self) -> None:
        rooms, loads = rots_sources.parse_world_rooms_and_loads(self.root)

        self.assertEqual(rooms[1100], "A Far Room")
        self.assertEqual(loads[7], [{"room": 1100, "zone_file": "10.zon", "zone": "Room Zone",
                                     "loaded_by": "Loader Zone"}])

    def test_descriptions_mention_the_loading_zone_only_when_it_differs(self) -> None:
        far_load = {"room": 1100, "room_name": "A Far Room", "zone": "Room Zone", "zone_file": "10.zon",
                    "loaded_by": "Loader Zone"}
        home_load = dict(far_load, zone="Loader Zone")

        self.assertEqual(class_packs.describe_load(far_load),
                         "room 1100 'A Far Room' (Room Zone, loaded by Loader Zone, 10.zon)")
        self.assertEqual(class_packs.describe_load(home_load), "room 1100 'A Far Room' (Loader Zone, 10.zon)")
        self.assertEqual(trainer_tables.describe_location(far_load),
                         "A Far Room (room 1100), Room Zone (loaded by the Loader Zone zone)")
        self.assertEqual(trainer_tables.describe_location(home_load), "A Far Room (room 1100), Loader Zone")


class CommandTableTest(unittest.TestCase):
    def setUp(self) -> None:
        self.rows = command_table.parse_registrations(INTERPRETER_FIXTURE, {"CMD_DEFEND": 5})

    def test_registrations_resolve_numbers_and_names(self) -> None:
        self.assertEqual([(row["number"], row["name"]) for row in self.rows],
                         [(1, "north"), (2, "east"), (4, "look"), (5, "defend")])
        self.assertEqual(self.rows[2]["level"], "LEVEL_GOD + 1")
        self.assertEqual(self.rows[2]["target1"], "TAR_CHAR_ROOM | TAR_OBJ_ROOM")

    def test_gaps_and_sort_stop(self) -> None:
        self.assertEqual(command_table.unregistered_numbers(self.rows), [3])
        self.assertEqual(command_table.sort_stop(self.rows, 350), 2)

    def test_report_has_tallies_and_gap_names(self) -> None:
        report = command_table.format_report(self.rows, command_table.parse_command_names(INTERPRETER_FIXTURE),
                                             350, include_table=True)

        self.assertIn("| 5 (CMD_DEFEND) | defend | POSITION_FIGHTING | do_defend | 0 | TRUE | 0 | FULL_TARGET | "
                      "FULL_TARGET | 0 |", report)
        self.assertIn("   2  POSITION_STANDING", report)
        self.assertIn("   3  ''", report)
        self.assertIn("both target masks FULL_TARGET: 3", report)
        self.assertEqual(report[-1], "sort_commands() stops after 2: commands 3 and above are not in its sorted list")

    def test_unknown_number_name_is_refused(self) -> None:
        with self.assertRaises(ValueError):
            command_table.parse_registrations(INTERPRETER_FIXTURE, {})


WAIT_FIXTURE = """#define SLOW(ch) WAIT_STATE(ch, 99)
void do_x(char_data* ch)
{
    WAIT_STATE(ch, 4);
    // WAIT_STATE(ch, 7);
    /* a block comment naming WAIT_STATE(ch, for this case) */
    WAIT_STATE_FULL(ch, number(2, (level + 1) / 2), CMD_SEARCH, (cmd == CMD_KNOCK) ? 1 : 2, 30, 0,
        GET_ABS_NUM(victim), victim /* the target, (sic) */, AFF_WAITING | AFF_WAITWHEEL, TARGET_CHAR);
    WAIT_STATE_BRIEF(ch, skills[SKILL_HIDE].beats, CMD_HIDE, 0, 59, AFF_WAITING);
}
"""


class WaitCallsTest(unittest.TestCase):
    def test_calls_are_read_with_nested_arguments_and_comments_skipped(self) -> None:
        calls = wait_calls.wait_calls_in(WAIT_FIXTURE, "src/x.cpp")

        self.assertEqual([(call["line"], call["macro"]) for call in calls],
                         [(4, "WAIT_STATE"), (7, "WAIT_STATE_FULL"), (9, "WAIT_STATE_BRIEF")])
        self.assertEqual((calls[0]["cycle"], calls[0]["priority"], calls[0]["flags"]), ("4", "50", "AFF_WAITING"))
        self.assertEqual((calls[1]["cycle"], calls[1]["command"], calls[1]["subcommand"], calls[1]["priority"],
                          calls[1]["flags"]),
                         ("number(2, (level + 1) / 2)", "CMD_SEARCH", "(cmd == CMD_KNOCK) ? 1 : 2", "30",
                          "AFF_WAITING | AFF_WAITWHEEL"))
        self.assertEqual((calls[2]["cycle"], calls[2]["priority"], calls[2]["flags"]),
                         ("skills[SKILL_HIDE].beats", "59", "AFF_WAITING"))

    def test_grouping_by_priority_orders_numerically(self) -> None:
        lines = wait_calls.format_by_priority(wait_calls.wait_calls_in(WAIT_FIXTURE, "src/x.cpp"))

        self.assertEqual([line for line in lines if line.startswith("==")],
                         ["== priority 30 (1 calls)", "== priority 50 (1 calls)", "== priority 59 (1 calls)"])
        self.assertIn("prio=50 (implicit)", lines[3])


HANDLER_FIXTURE = """ACMD(do_hide);
ACMD(do_hide)
{
    int skill = GET_SKILL(ch, SKILL_HIDE);
    // if (GET_SKILL(ch, SKILL_SNEAK))
    if (IS_SHADOW(ch))
        return;
}

void do_jig(char_data* ch, char* argument, waiting_type* wait_list, int cmd, int subcmd)
{
    send_to_char("You jig.", ch);
}
"""


class HandlerGatesTest(unittest.TestCase):
    def setUp(self) -> None:
        self.sources = {"src/x.cpp": HANDLER_FIXTURE.split("\n")}

    def test_gate_lines_are_reported_within_the_body(self) -> None:
        pattern = handler_gates.re.compile(handler_gates.DEFAULT_PATTERN)

        report = handler_gates.handler_report("hide", self.sources, pattern)

        self.assertEqual(report, ["== do_hide src/x.cpp:2-8", "   4: int skill = GET_SKILL(ch, SKILL_HIDE);",
                                  "   6: if (IS_SHADOW(ch))"])

    def test_pattern_can_be_overridden_and_missing_handlers_reported(self) -> None:
        pattern = handler_gates.re.compile(r"send_to_char")

        self.assertEqual(handler_gates.handler_report("jig", self.sources, pattern),
                         ["== do_jig src/x.cpp:10-13", '   12: send_to_char("You jig.", ch);'])
        self.assertEqual(handler_gates.handler_report("bash", self.sources, pattern), ["== do_bash NOT FOUND"])

    def test_all_handlers_are_the_acmd_definitions(self) -> None:
        self.assertEqual(handler_gates.all_handler_names(self.sources), ["hide"])


class PracticeCurveTest(unittest.TestCase):
    def test_warrior_skill_of_difficulty_thirty(self) -> None:
        values = practice_curve.curve(15, 30, True, 1000, 0, 0)

        self.assertEqual(values[:3], [12, 25, 36])
        self.assertEqual(values[14], 101)

    def test_warrior_skill_of_difficulty_ten(self) -> None:
        self.assertEqual(practice_curve.knowledge(4, 10), 96)
        self.assertEqual(practice_curve.knowledge(5, 10), 101)
        self.assertEqual(practice_curve.knowledge(6, 10), 100)

    def test_zero_difficulty_counts_as_ten(self) -> None:
        self.assertEqual(practice_curve.knowledge(4, 0), 96)

    def test_other_professions_scale_by_coefficient_and_level(self) -> None:
        # coefficient 700, level 5: 700 - 5 * 300 / 30 = 650; 650 * 85 / 100 + 200 - 50 = 702.
        self.assertEqual(practice_curve.adjusted_coefficient(700, 5), 702)
        self.assertEqual(practice_curve.knowledge(1, 30, warrior=False, coefficient=700, skill_level=5), 9)
        self.assertEqual(practice_curve.knowledge(1, 30, warrior=False, coefficient=1000, skill_level=0), 12)

    def test_weapon_mastery_adds_three_eighths_of_its_sessions(self) -> None:
        # 1 session + 8 mastery sessions: 20 + 8 * 20 * 3 / 8 = 80 units.
        self.assertEqual(practice_curve.knowledge(1, 30, mastery_sessions=8), 46)

    def test_above_one_hundred_is_flagged(self) -> None:
        lines = practice_curve.format_curve([96, 101])

        self.assertTrue(lines[2].endswith("above 100"))
        self.assertFalse(lines[1].endswith("above 100"))


class MobCensusTest(unittest.TestCase):
    def test_mobs_are_counted_by_field(self) -> None:
        mobs = rots_sources.parse_mob_file(MOB_FILE, "1.mob")

        self.assertEqual(mob_census.census(mobs, "level"), {20: 2, 15: 1})
        self.assertEqual(mob_census.census(mobs, "will_teach"), {63: 2, 0: 1})
        self.assertEqual(mob_census.vnums_with(mobs, "level", 20), [100, 102])


if __name__ == "__main__":
    unittest.main()
