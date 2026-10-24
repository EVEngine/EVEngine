"""Detect duplicated zeroerr case names.

CTest is keyed by test name, so two cases that share one name become one ambiguous entry
for every name-based filter: `ctest -R "^Scene\\."` silently selects both, `--testcase=X`
runs both, and a count like "170/170" stops meaning what it looks like. zeroerr itself does
not object -- it registers cases into a set ordered by (file, line), so both survive -- which
is exactly the situation `scripts/check_test_case_lines.py` guards from the other side.

The tree currently has no duplicates (5326 registered, 5326 distinct), so this checker needs
no baseline: a duplicate name is always an error. Sweep it as `--list` to see the names.

Names come from the three idioms the test tree uses:

  * `TEST_CASE("name")`
  * `TEST_CASE_FIXTURE(Fixture, "name")`
  * `TestScript(func)` in the script-test files, whose local macro expands to
    `TEST_CASE_FIXTURE(Fixture, "Fixture." #func)` with `Fixture` taken from the file's
    `UnitSciptTest(Fixture, content)` / `UnitReloadScriptTest(Fixture, content)`.

Optional `--binary <unit_test.exe>` compares the declared names with the ones the linked
binary actually registered (`--list-test-cases`), which also catches names that never
register for reasons other than a shared line. That mode needs a build, so it is *not* wired
into `check/quality`; the plan document records the question of enforcing it in CI.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

import check_test_assert_level
import utf8_stdio

SOURCE_ROOT = Path("test")

CASE_MACRO = re.compile(
    r"\bTEST_CASE\s*\(|\bTEST_CASE_FIXTURE\s*\(\s*[A-Za-z_]\w*\s*,|\bTEST_CASE_METHOD\s*\(\s*[A-Za-z_]\w*\s*,"
)
FIXTURE_MACRO = re.compile(r"\bUnit(?:Scipt|ReloadScript)Test\s*\(\s*([A-Za-z_]\w*)")
SCRIPT_CASE_MACRO = re.compile(r"\bTestScript\s*\(\s*([A-Za-z_]\w*)\s*\)")


def read_string_literal(text: str, start: int) -> str | None:
    """Parse the C string literal that begins at or after `start`; None when there is none.

    The case *name* is a literal, so it has to be read from the original text: the mask
    blanks literals (that is how a `TEST_CASE(` inside a comment or a raw string is ignored).
    Offsets are stable across masking, which is what makes this pairing safe.
    """
    index = start
    while index < len(text) and text[index] in " \t\r\n":
        index += 1
    if index >= len(text) or text[index] != '"':
        return None
    index += 1
    value: list[str] = []
    while index < len(text):
        char = text[index]
        if char == "\\" and index + 1 < len(text):
            value.append(text[index + 1])
            index += 2
            continue
        if char == '"':
            return "".join(value)
        value.append(char)
        index += 1
    return None


def in_macro_definition(masked: str, position: int) -> bool:
    """True when `position` sits inside a `#define` body, continuation lines included.

    The script-test files define their own case macro
    (`#define TestScript(func) \\` then `TEST_CASE_FIXTURE(FilesystemTest, "FilesystemTest." #func)`),
    so the macro *definition* looks like a case declaration with a strange name -- and its body
    is on the line *after* the `#define`. Only invocations declare cases.
    """
    lines = masked.splitlines(keepends=True)
    consumed = 0
    index = 0
    for number, line in enumerate(lines):
        consumed += len(line)
        if consumed > position:
            index = number
            break
    while index >= 0:
        stripped = lines[index].strip()
        if stripped.startswith("#define"):
            return True
        # A line continued with a trailing backslash belongs to the previous line's macro.
        if index > 0 and lines[index - 1].rstrip().endswith("\\"):
            index -= 1
            continue
        return False
    return False


def declared_names(path: Path) -> list[tuple[str, int]]:
    """Return (case name, line) for every case the file declares."""
    text = path.read_text(encoding="utf-8")
    masked = check_test_assert_level.mask_non_code(text)
    found: list[tuple[str, int]] = []
    for match in CASE_MACRO.finditer(masked):
        if in_macro_definition(masked, match.start()):
            continue
        name = read_string_literal(text, match.end())
        if name is not None:
            found.append((name, text.count("\n", 0, match.start()) + 1))
    fixtures = list(FIXTURE_MACRO.finditer(masked))
    fixture_index = -1
    for match in SCRIPT_CASE_MACRO.finditer(masked):
        while fixture_index + 1 < len(fixtures) and fixtures[fixture_index + 1].start() < match.start():
            fixture_index += 1
        if fixture_index < 0 or in_macro_definition(masked, match.start()):
            continue
        base = fixtures[fixture_index].group(1)
        name = f"{base}.{match.group(1)}"
        found.append((name, text.count("\n", 0, match.start()) + 1))
    return found


def scan(root: Path) -> dict[str, list[str]]:
    """Return name -> ["file:line", ...] for names declared more than once."""
    locations: dict[str, list[str]] = defaultdict(list)
    for path in sorted(root.rglob("*.cpp")):
        for name, line in declared_names(path):
            locations[name].append(f"{path.as_posix()}:{line}")
    return {name: places for name, places in locations.items() if len(places) > 1}


def registered_names(binary: Path) -> set[str]:
    """Case names the linked test binary actually registered."""
    result = subprocess.run(
        [str(binary), "--list-test-cases", "--list-format=plain"],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        check=False,
    )
    names = set()
    for line in result.stdout.splitlines():
        if line.strip():
            names.add(line.split("\t")[0].strip())
    return names


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=SOURCE_ROOT, help="directory to scan")
    parser.add_argument("--list", action="store_true", help="print every declared name")
    parser.add_argument(
        "--binary",
        type=Path,
        default=None,
        help="unit_test executable: also compare declared names with registered ones",
    )
    args = parser.parse_args()

    if args.list:
        for path in sorted(args.root.rglob("*.cpp")):
            for name, line in declared_names(path):
                print(f"{path.as_posix()}:{line}: {name}")

    duplicates = scan(args.root)
    for name, places in sorted(duplicates.items()):
        print(f"DUPLICATE case name '{name}' declared {len(places)} times: {', '.join(places)}")

    if args.binary is not None:
        declared = {
            name for path in sorted(args.root.rglob("*.cpp")) for name, _ in declared_names(path)
        }
        registered = registered_names(args.binary)
        missing = sorted(declared - registered)
        extra = sorted(registered - declared)
        for name in missing[:20]:
            print(f"declared but not registered: {name}")
        for name in extra[:20]:
            print(f"registered but not declared in {args.root}: {name}")
        if missing or extra:
            print(
                f"registration comparison: {len(missing)} declared-only, {len(extra)} registered-only"
            )
            return 1
        print(
            f"registration comparison OK: {len(registered)} registered cases all declared in {args.root}"
        )

    if duplicates:
        print(f"test case name check: {len(duplicates)} duplicated names")
        return 1
    print("test case name check OK: every declared case name is unique")
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
