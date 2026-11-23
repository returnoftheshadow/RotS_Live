"""Unit tests that keep scenario files from copying what the shared rots_harness modules hold.

A scenario file fails here when it defines a module-level function with the name of a shared
helper, binds a module-level string or number literal equal to a shared constant, or defines its
own SETTLE_SECONDS equal to replies.SETTLE_SECONDS. Rooms, objects and mobs number apart, so a
number is compared with shared room numbers when its name starts with ROOM_ and with the other
shared numbers otherwise. A value that still only coincides with a shared one is listed in
ALLOWED_COINCIDENCES with the reason it is not a copy.
"""

from __future__ import annotations

import ast
import inspect
from collections.abc import Iterator
from pathlib import Path
from types import ModuleType

from rots_harness import (
    accounts,
    casting,
    game_log,
    game_text,
    immortal_commands,
    replies,
    saved_files,
    shaping,
    stat_reading,
    text_lines,
    world,
)

# The real scenarios folder, beside this unit folder.
SCENARIOS_DIR = Path(__file__).resolve().parents[1] / "scenarios"
# The shared modules whose public functions a scenario imports rather than writes again.
HELPER_MODULES = (
    replies,
    text_lines,
    stat_reading,
    immortal_commands,
    game_log,
    casting,
    accounts,
    saved_files,
    shaping,
)
# The shared modules whose string and number constants a scenario imports rather than copies.
CONSTANT_MODULES = (game_text, world, shaping, stat_reading, casting, accounts, saved_files, immortal_commands)
# The smallest number checked: below it, counts, levels and wear positions coincide by chance, while
# world vnums and the shaping records are all at least this.
SMALLEST_CHECKED_NUMBER = 1000
# The prefix of a room number's name, in world.py and in scenario files alike.
ROOM_PREFIX = "ROOM_"
# The name of the settle constant a scenario imports from replies when its settle is the default.
SETTLE_NAME = "SETTLE_SECONDS"
# Scenario constants whose value equals a shared constant's by coincidence, keyed by file and name,
# with the reason each is not a copy.
ALLOWED_COINCIDENCES = {
    ("test_follower_rent_cycles.py", "DUMMY_SHORT_DESCRIPTION"): (
        "mob 2708, zone 27's practice dummy, not world.MOB_PRACTICE_DUMMY (2404)"
    ),
    ("test_knocking.py", "GATEKEEPER_VNUM"): "mob 2400, not object 2400 (world.OBJECT_FLASK)",
    ("test_object_versions.py", "CAST_DONE_REPLY"): "a spell's own \"Ok.\", not force's, order's or purge's",
    ("test_resistances.py", "CAST_PAID_REPLY"): "a spell's own \"Ok.\", not force's, order's or purge's",
    ("test_shaping_zone_editor.py", "UNKNOWN_COMMAND_TYPE_REPLY"): (
        "the zone editor's own answer (shapezon.cpp), not command_interpreter's"
    ),
    ("test_skins.py", "STEWARD"): "mob 1136, not object 1136 (world.OBJECT_LEATHER_BAG)",
    ("test_summon.py", "STAY_ZONE_DROP_FORMAT"): (
        "the drop prohibit_item_stay_zone_move prints (act_move.cpp), not perform_drop's DROP_FORMAT"
    ),
    ("test_tame.py", "TARGET_ORC_VNUM"): (
        "mob 1130, the target orc, not object 1130 (world.OBJECT_HARNESS_TOKEN); shaping.SHAPED_RECORD "
        "names the shaping tests' record of both kinds"
    ),
}


def shared_functions() -> dict[str, str]:
    """Return each public function the helper modules define, mapped to its module.function name."""
    functions = {}
    for module in HELPER_MODULES:
        members = inspect.getmembers(module, inspect.isfunction)
        for name, function in members:
            if not name.startswith("_") and function.__module__ == module.__name__:
                functions[name] = f"{module.__name__}.{name}"

    return functions


def checked_value(value: object) -> bool:
    """Return whether a constant's value is one this guard compares: a string, or a large number."""
    if isinstance(value, bool):
        return False

    if isinstance(value, str):
        return True

    return isinstance(value, int) and value >= SMALLEST_CHECKED_NUMBER


def comparison_key(name: str, value: object) -> tuple[bool, object]:
    """Return the key a constant is compared by: its value, and for a number whether it is a room's.

    A string's key is the same whatever its name.
    """
    if isinstance(value, str):
        return False, value

    return name.startswith(ROOM_PREFIX), value


def shared_constants() -> dict[tuple[bool, object], list[str]]:
    """Return the names of the constant modules' checked constants, keyed by comparison_key."""
    constants: dict[tuple[bool, object], list[str]] = {}
    for module in CONSTANT_MODULES:
        bindings = vars(module).items()
        for name, value in bindings:
            if name.isupper() and not name.startswith("_") and checked_value(value):
                qualified = f"{module.__name__}.{name}"
                key = comparison_key(name, value)
                names = constants.setdefault(key, [])
                if qualified not in names:
                    names.append(qualified)

    return constants


def literal_bindings(tree: ast.Module) -> Iterator[tuple[str, object]]:
    """Yield each module-level name tree binds to a single literal, with the literal's value."""
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1:
            target = node.targets[0]
        elif isinstance(node, ast.AnnAssign) and node.value is not None:
            target = node.target
        else:
            continue

        if isinstance(target, ast.Name) and isinstance(node.value, ast.Constant):
            yield target.id, node.value.value


def copies_in(scenarios_dir: Path) -> list[str]:
    """Return one line for each copy of a shared helper or constant in scenarios_dir's test files.

    Each line names the file, what it defines and the shared name to import in its place.
    """
    functions = shared_functions()
    constants = shared_constants()
    copies = []
    scenario_paths = sorted(scenarios_dir.glob("test_*.py"))
    for path in scenario_paths:
        source = path.read_text(encoding="utf-8")
        tree = ast.parse(source)
        for node in tree.body:
            if isinstance(node, ast.FunctionDef) and node.name in functions:
                copies.append(f"{path.name}: def {node.name} copies {functions[node.name]}; import it")

        bindings = literal_bindings(tree)
        for name, value in bindings:
            if name == SETTLE_NAME and value == replies.SETTLE_SECONDS:
                copies.append(f"{path.name}: {name} copies rots_harness.replies.{SETTLE_NAME}; import it")
                continue

            if not checked_value(value) or (path.name, name) in ALLOWED_COINCIDENCES:
                continue

            key = comparison_key(name, value)
            if key in constants:
                shared_names = " or ".join(constants[key])
                copies.append(f"{path.name}: {name} = {value!r} copies {shared_names}; import it")

    return copies


def write_scenario(folder: Path, source: str) -> None:
    """Write source as the one scenario file in folder."""
    scenario_path = folder / "test_synthetic.py"
    scenario_path.write_text(source, encoding="utf-8")


def test_no_scenario_file_copies_a_shared_helper_or_constant() -> None:
    copies = copies_in(SCENARIOS_DIR)
    assert not copies, "\n".join(copies)


def test_every_allowed_coincidence_still_exists() -> None:
    stale = []
    for file_name, name in ALLOWED_COINCIDENCES:
        source = (SCENARIOS_DIR / file_name).read_text(encoding="utf-8")
        tree = ast.parse(source)
        bound = dict(literal_bindings(tree))
        if name not in bound:
            stale.append(f"{file_name}: {name}")

    assert not stale, f"remove these from ALLOWED_COINCIDENCES: {stale}"


def test_a_scenario_function_named_as_a_shared_helper_is_found(tmp_path: Path) -> None:
    write_scenario(tmp_path, 'def reply_to(session, line):\n    """Send line."""\n')
    copies = copies_in(tmp_path)
    assert copies == ["test_synthetic.py: def reply_to copies rots_harness.replies.reply_to; import it"]


def test_a_scenario_text_equal_to_a_shared_one_is_found(tmp_path: Path) -> None:
    write_scenario(tmp_path, 'SASH = "a draining grey sash"\nGIRDLE: str = "a stone girdle"\n')
    copies = copies_in(tmp_path)
    assert len(copies) == 2, copies
    assert "rots_harness.world.OBJECT_GREY_SASH_SHORT_DESCRIPTION" in copies[0], copies
    assert "rots_harness.world.OBJECT_STONE_GIRDLE_SHORT_DESCRIPTION" in copies[1], copies


def test_a_scenario_vnum_equal_to_a_shared_one_is_found_and_a_small_number_is_not(tmp_path: Path) -> None:
    write_scenario(tmp_path, "PORTER_VNUM = 1722\nROOM_HALL = 1601\nHOURS = 6\n")
    copies = copies_in(tmp_path)
    assert copies == [
        "test_synthetic.py: PORTER_VNUM = 1722 copies rots_harness.world.MOB_PORTER; import it",
        "test_synthetic.py: ROOM_HALL = 1601 copies rots_harness.world.ROOM_INN; import it",
    ]


def test_a_room_number_is_compared_with_rooms_only(tmp_path: Path) -> None:
    # 1304 is world.OBJECT_IRON_CAP and 1160 is world.ROOM_HUMAN_START.
    write_scenario(tmp_path, "ROOM_VAULT = 1304\nRING_VNUM = 1160\n")
    assert copies_in(tmp_path) == []


def test_a_text_several_shared_constants_hold_names_each_of_them(tmp_path: Path) -> None:
    write_scenario(tmp_path, 'DONE = "Ok."\n')
    copies = copies_in(tmp_path)
    assert len(copies) == 1, copies
    assert "game_text.FORCED_REPLY" in copies[0] and "game_text.PURGED_REPLY" in copies[0], copies


def test_a_default_settle_copy_is_found_and_an_editor_settle_is_not(tmp_path: Path) -> None:
    write_scenario(tmp_path, "SETTLE_SECONDS = 1.0\nEDITOR_SETTLE_SECONDS = 0.5\n")
    copies = copies_in(tmp_path)
    assert copies == ["test_synthetic.py: SETTLE_SECONDS copies rots_harness.replies.SETTLE_SECONDS; import it"]


def test_an_allowed_coincidence_and_an_unshared_text_are_not_found(tmp_path: Path) -> None:
    scenario_path = tmp_path / "test_tame.py"
    scenario_path.write_text('TARGET_ORC_VNUM = 1130\nGREETING = "Hello there."\n', encoding="utf-8")
    assert copies_in(tmp_path) == []


def test_a_derived_value_and_an_imported_name_are_not_found(tmp_path: Path) -> None:
    source = (
        "from rots_harness import world\n"
        "from rots_harness.game_text import WEAR_WAIST_FORMAT\n"
        "GIRDLE_WORN_REPLY = WEAR_WAIST_FORMAT.format(world.OBJECT_STONE_GIRDLE_SHORT_DESCRIPTION)\n"
    )
    write_scenario(tmp_path, source)
    assert copies_in(tmp_path) == []
