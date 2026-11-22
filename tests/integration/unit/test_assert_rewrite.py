"""Unit tests for conftest.py's register_assert_rewrite list: every rots_harness module a scenario
reaches that uses assert is on it, so its failures report their operands."""

from __future__ import annotations

import ast
from pathlib import Path

# tests/integration, which holds conftest.py, rots_harness and scenarios.
INTEGRATION_ROOT = Path(__file__).resolve().parents[1]
# The package the shared helpers live in.
HARNESS_PACKAGE = "rots_harness"


def parsed(path: Path) -> ast.Module:
    """Return path's source, parsed."""
    source = path.read_text(encoding="utf-8")
    return ast.parse(source)


def harness_imports(tree: ast.Module) -> set[str]:
    """Return the rots_harness modules tree imports, as dotted names."""
    modules = set()
    for node in ast.walk(tree):
        if isinstance(node, ast.ImportFrom) and node.module == HARNESS_PACKAGE:
            modules.update(f"{HARNESS_PACKAGE}.{alias.name}" for alias in node.names)
        elif isinstance(node, ast.ImportFrom) and (node.module or "").startswith(f"{HARNESS_PACKAGE}."):
            modules.add(node.module)

    return modules


def module_path(module: str) -> Path:
    """Return the file of a dotted rots_harness module name."""
    relative = module.replace(".", "/")
    return INTEGRATION_ROOT / f"{relative}.py"


def modules_scenarios_reach() -> set[str]:
    """Return every rots_harness module a scenario file imports, directly or through another one."""
    pending = set()
    scenario_paths = (INTEGRATION_ROOT / "scenarios").glob("test_*.py")
    for path in scenario_paths:
        scenario_tree = parsed(path)
        pending |= harness_imports(scenario_tree)

    reached = set()
    while pending:
        module = pending.pop()
        reached.add(module)
        path = module_path(module)
        module_tree = parsed(path)
        imported_modules = harness_imports(module_tree)
        pending |= {imported for imported in imported_modules if imported not in reached}

    return reached


def uses_assert(module: str) -> bool:
    """Return whether the module holds an assert statement."""
    path = module_path(module)
    tree = parsed(path)
    return any(isinstance(node, ast.Assert) for node in ast.walk(tree))


def registered_modules() -> set[str]:
    """Return the module names conftest.py passes to pytest.register_assert_rewrite."""
    conftest_path = INTEGRATION_ROOT / "conftest.py"
    tree = parsed(conftest_path)
    registered = set()
    for node in ast.walk(tree):
        is_call = isinstance(node, ast.Call) and isinstance(node.func, ast.Attribute)
        if is_call and node.func.attr == "register_assert_rewrite":
            registered.update(argument.value for argument in node.args if isinstance(argument, ast.Constant))

    return registered


def test_every_module_scenarios_reach_that_uses_assert_is_registered() -> None:
    reached = modules_scenarios_reach()
    needed = {module for module in reached if uses_assert(module)}
    registered = registered_modules()
    missing = sorted(needed - registered)
    assert not missing, f"add {missing} to conftest.py's register_assert_rewrite"


def test_every_registered_module_exists() -> None:
    registered = registered_modules()
    absent = sorted(module for module in registered if not module_path(module).is_file())
    assert not absent, f"conftest.py registers modules that do not exist: {absent}"


def test_the_shared_helpers_with_asserts_are_among_those_found() -> None:
    reached = modules_scenarios_reach()
    assert {"rots_harness.text_lines", "rots_harness.immortal_commands"} <= reached
    assert uses_assert("rots_harness.text_lines")
