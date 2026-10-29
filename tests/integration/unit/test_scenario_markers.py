from __future__ import annotations

import ast
from pathlib import Path

# The scenario files whose xfail markers are checked.
SCENARIOS_DIR = Path(__file__).resolve().parents[1] / "scenarios"


def is_xfail_marker(node: ast.AST) -> bool:
    """True for the expression pytest.mark.xfail."""
    return (
        isinstance(node, ast.Attribute)
        and node.attr == "xfail"
        and isinstance(node.value, ast.Attribute)
        and node.value.attr == "mark"
        and isinstance(node.value.value, ast.Name)
        and node.value.value.id == "pytest"
    )


def xfail_problems(source: str, file_name: str) -> list[str]:
    """Each pytest.mark.xfail in source that is not strict or does not limit its failure to
    raises=AssertionError, as "<file>:<line>: <what is missing>"."""
    problems = []
    called = set()
    tree = ast.parse(source)
    for node in ast.walk(tree):
        if isinstance(node, ast.Call) and is_xfail_marker(node.func):
            called.add(id(node.func))
            keywords = {keyword.arg: keyword.value for keyword in node.keywords}
            strict = keywords.get("strict")
            if not (isinstance(strict, ast.Constant) and strict.value is True):
                problems.append(f"{file_name}:{node.lineno}: xfail without strict=True")
            raises = keywords.get("raises")
            if not (isinstance(raises, ast.Name) and raises.id == "AssertionError"):
                problems.append(f"{file_name}:{node.lineno}: xfail without raises=AssertionError")
    for node in ast.walk(tree):
        if is_xfail_marker(node) and id(node) not in called:
            problems.append(f"{file_name}:{node.lineno}: bare xfail without strict=True and raises=AssertionError")
    return problems


def test_every_scenario_xfail_is_strict_and_fails_only_on_an_assertion() -> None:
    """A pin's xfail passes only when the pin's assertion fails: a crash, a missing fixture file or
    a typo raises something else and fails the run instead of hiding as the expected failure. The
    harness's own timeouts (SessionTimeout, LoginRefused) are AssertionErrors, so a pin may still
    fail by waiting for text that never comes."""
    problems = []
    for path in sorted(SCENARIOS_DIR.glob("*.py")):
        problems.extend(xfail_problems(path.read_text(encoding="utf-8"), path.name))
    assert problems == []


def test_a_strict_assertion_xfail_passes_the_check() -> None:
    source = '@pytest.mark.xfail(strict=True, raises=AssertionError, reason="r")\ndef test_x(): pass\n'
    assert xfail_problems(source, "t.py") == []


def test_an_xfail_without_raises_is_reported() -> None:
    source = '@pytest.mark.xfail(strict=True, reason="r")\ndef test_x(): pass\n'
    assert xfail_problems(source, "t.py") == ["t.py:1: xfail without raises=AssertionError"]


def test_a_non_strict_xfail_is_reported() -> None:
    source = '@pytest.mark.xfail(raises=AssertionError, reason="r")\ndef test_x(): pass\n'
    assert xfail_problems(source, "t.py") == ["t.py:1: xfail without strict=True"]


def test_an_xfail_raising_another_type_is_reported() -> None:
    source = '@pytest.mark.xfail(strict=True, raises=ConnectionError, reason="r")\ndef test_x(): pass\n'
    assert xfail_problems(source, "t.py") == ["t.py:1: xfail without raises=AssertionError"]


def test_an_xfail_inside_a_param_is_checked() -> None:
    source = 'CASES = [pytest.param(1, marks=pytest.mark.xfail(strict=True, reason="r"))]\n'
    assert xfail_problems(source, "t.py") == ["t.py:1: xfail without raises=AssertionError"]


def test_a_bare_xfail_is_reported() -> None:
    source = "@pytest.mark.xfail\ndef test_x(): pass\n"
    assert xfail_problems(source, "t.py") == ["t.py:1: bare xfail without strict=True and raises=AssertionError"]
