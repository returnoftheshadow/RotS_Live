"""Unit tests for rots_harness.shaping: each fact against the game's sources and the test world."""

from __future__ import annotations

from pathlib import Path

import pytest

from rots_harness import shaping, world
from rots_harness.session import SessionTimeout
from unit.fakes import RecordingSession

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
        (shaping.MOBILE_RELEASED_REPLY, "shapemob.cpp"),
        (shaping.OBJECT_RELEASED_REPLY, "shapeobj.cpp"),
        (shaping.ROOM_RELEASED_REPLY, "shaperom.cpp"),
        (shaping.ZONE_RELEASED_REPLY, "shapezon.cpp"),
        (shaping.SCRIPT_RELEASED_REPLY, "shapescript.cpp"),
    ],
)
def test_each_release_marker_is_what_its_editor_prints(released: str, file_name: str) -> None:
    assert f'send_to_char("{released}\\n\\r", ch);' in source_text(file_name)


@pytest.mark.parametrize(
    ("prompt", "prompt_format", "record"),
    [
        (shaping.IMMORTAL_START_ROOM_PROMPT, "Shaping: %d", world.ROOM_IMMORTAL_START),
        (shaping.SIGNPOST_ROOM_PROMPT, "Shaping: %d", world.ROOM_SIGNPOST),
        (shaping.MOBILE_PROMPT, "Mobile: %d", shaping.SHAPED_RECORD),
        (shaping.OBJECT_PROMPT, "Object: %d", shaping.SHAPED_RECORD),
        (shaping.ZONE_PROMPT, "Zone: %d", shaping.ZONE_NUMBER),
        (shaping.SCRIPT_PROMPT, "Script: %d", shaping.SHAPED_SCRIPT),
        (shaping.ORDERED_SCRIPT_PROMPT, "Script: %d", shaping.ORDERED_SCRIPT),
    ],
)
def test_each_prompt_is_prompt_texts_line_for_its_record(prompt: str, prompt_format: str, record: int) -> None:
    assert f'"{prompt_format}"' in source_text("consts.cpp")
    assert prompt == prompt_format.replace("%d", str(record)) + ">"


def test_the_section_prompts_are_shape_section_prompts() -> None:
    shapemob_text = source_text("shapemob.cpp")
    assert f'"{shaping.SECTION_TITLE_PROMPT_START}, at most' in shapemob_text
    assert f'"{shaping.SECTION_COMMENT_PROMPT_START}, shown under' in shapemob_text


def test_the_shaped_records_are_in_the_test_world() -> None:
    world_folder = INTEGRATION_ROOT / "world"
    records = (
        ("mob/11.mob", shaping.SHAPED_RECORD),
        ("obj/11.obj", shaping.SHAPED_RECORD),
        ("scr/11.scr", shaping.SHAPED_SCRIPT),
        ("scr/11.scr", shaping.ORDERED_SCRIPT),
    )
    for relative, record in records:
        record_text = (world_folder / relative).read_text(encoding="latin-1")
        record_lines = record_text.splitlines()
        assert any(line.split()[:1] == [f"#{record}"] for line in record_lines if line), relative

    assert (INTEGRATION_ROOT / shaping.ZONE_FILE_RELATIVE).is_file()
    assert shaping.ZONE_FILE_RELATIVE == Path("world") / "zon" / "11.zon"
    assert (INTEGRATION_ROOT / shaping.SCRIPT_FILE_RELATIVE).is_file()
    assert shaping.SCRIPT_FILE_RELATIVE == Path("world") / "scr" / "11.scr"


@pytest.mark.parametrize(
    ("text", "file_name"),
    [
        (shaping.COULD_NOT_OPEN_FILE_REPLY, "shapemob.cpp"),
        (shaping.COULD_NOT_OPEN_FILE_REPLY, "shapeobj.cpp"),
        (shaping.COULD_NOT_OPEN_FILE_REPLY, "shaperom.cpp"),
        (shaping.COULD_NOT_OPEN_FILE_REPLY, "shapezon.cpp"),
        (shaping.COULD_NOT_OPEN_FILE_REPLY, "shapescript.cpp"),
        (shaping.NO_BACKUP_FILE_START, "shapemob.cpp"),
        (shaping.NO_BACKUP_FILE_START, "shapeobj.cpp"),
        (shaping.NO_BACKUP_FILE_START, "shaperom.cpp"),
        (shaping.NO_BACKUP_FILE_START, "shapezon.cpp"),
        (shaping.NO_BACKUP_FILE_START, "shapescript.cpp"),
        (shaping.ROOM_IMPLEMENTED_REPLY, "shaperom.cpp"),
        (shaping.ZONE_IMPLEMENTED_REPLY, "shapezon.cpp"),
    ],
)
def test_each_editor_text_is_one_its_editor_prints(text: str, file_name: str) -> None:
    assert f'{text}\\n\\r"' in source_text(file_name)


def test_the_editor_settle_is_half_a_second() -> None:
    assert shaping.EDITOR_SETTLE_SECONDS == 0.5


def test_release_editor_sends_free_and_returns_the_reply_after_the_editor_settle() -> None:
    session = RecordingSession([shaping.ROOM_RELEASED_REPLY], "\n\rS:Surging> ")
    released = shaping.release_editor(session, shaping.ROOM_RELEASED_REPLY)
    assert released == shaping.ROOM_RELEASED_REPLY + "\n\rS:Surging> "
    assert session.calls == [("send_line", "/free"), ("expect", [shaping.ROOM_RELEASED_REPLY], {}), ("drain", 0.5)]


def test_release_editor_passes_a_given_settle_by_keyword_only() -> None:
    session = RecordingSession([shaping.ZONE_RELEASED_REPLY])
    shaping.release_editor(session, shaping.ZONE_RELEASED_REPLY, settle=1.0)
    assert ("drain", 1.0) in session.calls
    with pytest.raises(TypeError):
        shaping.release_editor(session, shaping.ZONE_RELEASED_REPLY, 1.0)  # type: ignore[misc]


def test_release_editor_lets_a_timeout_through() -> None:
    session = RecordingSession()
    with pytest.raises(SessionTimeout):
        shaping.release_editor(session, shaping.MOBILE_RELEASED_REPLY)
