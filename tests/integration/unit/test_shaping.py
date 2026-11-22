"""Unit tests for rots_harness.shaping: each fact against the game's sources and the test world."""

from __future__ import annotations

from pathlib import Path

import pytest

from rots_harness import shaping, world

# The repository root, where the game's sources are.
REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
# tests/integration, whose world folder is the test world the lib builder copies.
INTEGRATION_ROOT = Path(__file__).resolve().parents[1]


def source_text(file_name: str) -> str:
    """The text of one of the game's source files."""
    return (REPOSITORY_ROOT / "src" / file_name).read_text(encoding="latin-1")


@pytest.mark.parametrize(
    ("released", "file_name"),
    [
        (shaping.MOBILE_RELEASED, "shapemob.cpp"),
        (shaping.OBJECT_RELEASED, "shapeobj.cpp"),
        (shaping.ROOM_RELEASED, "shaperom.cpp"),
        (shaping.ZONE_RELEASED, "shapezon.cpp"),
        (shaping.SCRIPT_RELEASED, "shapescript.cpp"),
    ],
)
def test_each_release_marker_is_what_its_editor_prints(released: str, file_name: str) -> None:
    assert f'send_to_char("{released}\\n\\r", ch);' in source_text(file_name)


@pytest.mark.parametrize(
    ("prompt", "prompt_format", "record"),
    [
        (shaping.IMMORTAL_START_ROOM_PROMPT, "Shaping: %d", world.ROOM_IMMORTAL_START),
        (shaping.MOBILE_PROMPT, "Mobile: %d", shaping.SHAPED_RECORD),
        (shaping.OBJECT_PROMPT, "Object: %d", shaping.SHAPED_RECORD),
        (shaping.ZONE_PROMPT, "Zone: %d", shaping.ZONE_NUMBER),
        (shaping.SCRIPT_PROMPT, "Script: %d", shaping.SHAPED_SCRIPT),
    ],
)
def test_each_prompt_is_prompt_texts_line_for_its_record(prompt: str, prompt_format: str, record: int) -> None:
    assert f'"{prompt_format}"' in source_text("consts.cpp")
    assert prompt == prompt_format.replace("%d", str(record)) + ">"


def test_the_section_prompts_are_shape_section_prompts() -> None:
    shapemob_text = source_text("shapemob.cpp")
    assert f'"{shaping.SECTION_TITLE_PROMPT}, at most' in shapemob_text
    assert f'"{shaping.SECTION_COMMENT_PROMPT}, shown under' in shapemob_text


def test_the_shaped_records_are_in_the_test_world() -> None:
    world_folder = INTEGRATION_ROOT / "world"
    for relative, record in (("mob/11.mob", shaping.SHAPED_RECORD), ("obj/11.obj", shaping.SHAPED_RECORD), ("scr/11.scr", shaping.SHAPED_SCRIPT)):
        record_lines = (world_folder / relative).read_text(encoding="latin-1").splitlines()
        assert any(line.split()[:1] == [f"#{record}"] for line in record_lines if line), relative
    assert (INTEGRATION_ROOT / shaping.ZONE_FILE_RELATIVE).is_file()
    assert shaping.ZONE_FILE_RELATIVE == Path("world") / "zon" / "11.zon"
