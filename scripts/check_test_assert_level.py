"""Detect test cases that can never fail because every assertion is warning-level.

zeroerr maps `CHECK*` to `ZEROERR_WARN` and `REQUIRE*` / `ASSERT*` to
`ZEROERR_ERROR` / `ZEROERR_FATAL` (`external/zeroerr/include/zeroerr/assert.h:148-183`).
A case whose assertions are all `CHECK*` therefore prints `PASSED n | WARNING m`, exits 0,
and CTest reports it as passed: the case cannot fail, so it cannot guard anything.

That is not hypothetical in this repository:

  * `devtools.snapshot.rejectsInstanceRoot` and
    `devtools.snapshot.skipsInstanceRootInAutoScan` asserted *opposite* outcomes of the same
    behaviour and both reported green;
  * several fixes in this repository survived their own counterfactual because the
    assertion that should have gone red was a `CHECK`;
  * `ScriptECS.shaderSystemClassExists` returned false for its whole life unnoticed.

This checker is source-only. It freezes the cases that are already in that shape in a
baseline and fails when a new one appears, so a test can no longer be *added* in a shape
that can never fail. Removing the frozen entries is the separate sweep tracked by the plan
document (`docs/dev/2026-09-19-...md` 10.5).

Recognized escape hatch: a case that delegates to a helper *defined in the same file* which
asserts with `REQUIRE`/`ASSERT` can fail, so it is not reported. Delegation to a helper in
another file or a header cannot be seen from the source alone; such a case is reported, and
the author can either state the decisive assertion with `REQUIRE` or record the case in the
baseline.

The baseline freezes *whole* cases only, so one incidental `REQUIRE(x != nullptr)` exempts a
case whose decisive assertions are all `CHECK`. Files listed in `STRICT_FILES` are the
exception: they are evidence for a contract rather than incidental module coverage, so a
warning-level assertion there is a failure even when the case has a `REQUIRE` too.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections.abc import Sequence
from pathlib import Path

import utf8_stdio

BASELINE = Path("scripts/test_assert_level_baseline.json")
SOURCE_ROOT = Path("test")

# Files whose cases are the *evidence* for a contract, so every one of them must be able to
# fail. Add a file here only when all of its cases are evidence; a module's general coverage
# belongs in the baseline instead. `test/script_provider.cpp` earned its place when
# `ScriptProvider.failedBindIsRolledBack` asserted the documented rollback with `CHECK`s
# while the rollback never ran (plan doc 2026-09-19-...md 9.50/9.51).
STRICT_FILES: tuple[str, ...] = (
    "test/script_provider.cpp",
    "test/reload_script.cpp",
)

CASE_MACRO = re.compile(r"\b(TEST_CASE|TEST_CASE_FIXTURE|TEST_CASE_METHOD)\s*\(")
# Warning-level: the case keeps passing. Anything CHECK*-shaped is ZEROERR_WARN.
WARN_ASSERT = re.compile(r"\bCHECK[A-Z_]*\s*\(")
# Failure-level: REQUIRE* -> ZEROERR_ERROR, ASSERT* -> ZEROERR_FATAL. The engine macros are
# zeroerr-backed as well (src/engine/common/Assert.h), so they fail the case too.
FAIL_ASSERT = re.compile(r"\b(REQUIRE[A-Z_]*|ASSERT[A-Z_]*|EV_ASSERT|EV_PARAM_CHECK)\s*\(")
# A function definition in the same file; its body is scanned for failure-level assertions.
FUNCTION_DEF = re.compile(
    r"(?:^|\n)[ \t]*(?:template\s*<[^;{}]*>\s*)?"
    r"(?:static\s+|inline\s+|constexpr\s+|const\s+)*"
    r"[A-Za-z_][\w:<>,\s*&]*?[ \t*&]([A-Za-z_]\w*)\s*\([^;{}]*\)\s*"
    r"(?:const\s*)?(?:noexcept\s*)?\{"
)


def mask_non_code(text: str) -> str:
    """Blank out comments, string and character literals, keeping every offset stable.

    Test bodies contain raw strings (`R"SQ(...)SQ"`) full of braces, and both Squirrel and
    JSON snippets inside them, so brace matching and macro search have to run on masked text.
    """
    out = list(text)
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out[i] = " " if text[i] != "\n" else "\n"
                i += 1
            if i < n:
                out[i] = out[i + 1] = " "
                i += 2
            continue
        if ch == "R" and i + 1 < n and text[i + 1] == '"':
            end = text.find("(", i + 2)
            if end != -1:
                delim = text[i + 2 : end]
                terminator = ")" + delim + '"'
                close = text.find(terminator, end)
                stop = n if close == -1 else close + len(terminator)
                for k in range(i, stop):
                    if out[k] != "\n":
                        out[k] = " "
                i = stop
                continue
        if ch == "'":
            # A C++14 digit separator (`1'000'000`) looks like the start of a character
            # literal to a naive scanner, and treating it as one blanks every character up to
            # the next apostrophe -- which hid whole cases from this checker. Two digit
            # neighbours are unambiguous.
            if 0 < i < n - 1 and text[i - 1].isdigit() and text[i + 1].isdigit():
                i += 1
                continue
            # Otherwise scan a *bounded* character literal (`'a'`, `'\n'`, `'\''`, `'ab'`):
            # a stray apostrophe must not consume the rest of the file.
            closing = -1
            j = i + 1
            while j < n and j - i <= 8 and text[j] != "\n":
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == "'":
                    closing = j
                    break
                j += 1
            if closing <= i + 1:
                i += 1
                continue
            for k in range(i, closing + 1):
                out[k] = " "
            i = closing + 1
            continue
        if ch == '"':
            quote = ch
            out[i] = " "
            i += 1
            while i < n:
                if text[i] == "\\":
                    out[i] = " "
                    if i + 1 < n and out[i + 1] != "\n":
                        out[i + 1] = " "
                    i += 2
                    continue
                out[i] = " " if text[i] != "\n" else "\n"
                if text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        i += 1
    return "".join(out)


def match_brace(masked: str, start: int) -> int:
    """Return the index of the `}` closing the body that starts at `start` (a `{`)."""
    depth = 0
    for i in range(start, len(masked)):
        if masked[i] == "{":
            depth += 1
        elif masked[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return -1


def failing_helpers(masked: str) -> list[str]:
    """Names of same-file functions whose body contains a failure-level assertion."""
    names: list[str] = []
    for match in FUNCTION_DEF.finditer(masked):
        open_brace = masked.find("{", match.end() - 1)
        close = match_brace(masked, open_brace)
        if close == -1:
            continue
        if FAIL_ASSERT.search(masked[open_brace : close + 1]):
            names.append(match.group(1))
    return names


def case_name(text: str, macro_end: int) -> str:
    """First string literal after the case macro, or `<unnamed>`."""
    tail = text[macro_end : macro_end + 400]
    literal = re.search(r'"((?:[^"\\]|\\.)*)"', tail)
    return literal.group(1) if literal else "<unnamed>"


def scan_file(path: Path, helpers: list[str]) -> list[tuple[int, str]]:
    """Return (line, case name) for every case whose assertions are all warning-level."""
    text = path.read_text(encoding="utf-8")
    masked = mask_non_code(text)
    offenders: list[tuple[int, str]] = []
    for match in CASE_MACRO.finditer(masked):
        body_start = masked.find("{", match.end())
        if body_start == -1:
            continue
        body_end = match_brace(masked, body_start)
        if body_end == -1:
            continue
        body = masked[body_start : body_end + 1]
        if not WARN_ASSERT.search(body) or FAIL_ASSERT.search(body):
            continue
        if any(re.search(rf"\b{re.escape(name)}\s*\(", body) for name in helpers):
            continue
        line = text.count("\n", 0, match.start()) + 1
        offenders.append((line, case_name(text, match.end())))
    return offenders


def scan(root: Path) -> list[str]:
    """Return sorted `relative/path|case name|occurrence` keys for every offender."""
    keys: list[str] = []
    for path in sorted(root.rglob("*.cpp")):
        masked = mask_non_code(path.read_text(encoding="utf-8"))
        offenders = scan_file(path, failing_helpers(masked))
        seen: dict[str, int] = {}
        for _, name in offenders:
            seen[name] = seen.get(name, 0) + 1
            keys.append(f"{path.as_posix()}|{name}|{seen[name]}")
    return sorted(keys)


def enclosing_case(masked: str, text: str, index: int) -> str:
    """Name of the case whose body contains `index` (the last one that does)."""
    name = "<outside any case>"
    for match in CASE_MACRO.finditer(masked):
        if match.start() > index:
            break
        body_start = masked.find("{", match.end())
        if body_start == -1:
            continue
        body_end = match_brace(masked, body_start)
        if body_end == -1 or body_end < index:
            continue
        name = case_name(text, match.end())
    return name


def strict_violations(files: Sequence[str] = STRICT_FILES) -> list[str]:
    """Warning-level assertions in a file that has to be able to fail.

    Returns `path|case|line`, or `path|<missing>|0` when the file is gone, so the list
    cannot silently stop covering anything.
    """
    found: list[str] = []
    for name in files:
        path = Path(name)
        if not path.exists():
            found.append(f"{name}|<missing>|0")
            continue
        text = path.read_text(encoding="utf-8")
        masked = mask_non_code(text)
        for match in WARN_ASSERT.finditer(masked):
            line = text.count("\n", 0, match.start()) + 1
            found.append(f"{name}|{enclosing_case(masked, text, match.start())}|{line}")
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--write-baseline", action="store_true", help="record the current offenders"
    )
    parser.add_argument("--root", type=Path, default=SOURCE_ROOT, help="directory to scan")
    parser.add_argument("--list", action="store_true", help="print every offender with its line")
    args = parser.parse_args()

    found = scan(args.root)
    if args.write_baseline:
        BASELINE.write_text(
            json.dumps({"schema_version": 1, "sites": found}, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        print(f"warning-only test cases: wrote {len(found)} entries to {BASELINE}")
        return 0

    if args.list:
        for path in sorted(args.root.rglob("*.cpp")):
            masked = mask_non_code(path.read_text(encoding="utf-8"))
            for line, name in scan_file(path, failing_helpers(masked)):
                print(f"{path.as_posix()}:{line}: {name}")

    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))["sites"]
    known = set(baseline)
    current = set(found)
    added = sorted(current - known)
    stale = sorted(known - current)
    strict = strict_violations()

    for key in added:
        print(f"NEW warning-only test case (add a REQUIRE for the decisive assertion): {key}")
    for key in strict:
        path, case, line = key.split("|")
        if case == "<missing>":
            print(f"must-fail evidence file is missing (update STRICT_FILES): {path}")
        else:
            print(
                f"warning-level assertion in a must-fail evidence file "
                f"({path}:{line}, {case}): use REQUIRE so the case can fail"
            )
    # Stale entries are informational, not failures: an entry goes stale when a case is
    # renamed, removed or made fail-able, and none of those needs re-review. Failing on them
    # would make every test rename touch this baseline. `--write-baseline` prunes them.
    for key in stale:
        print(f"note: baselined case changed or removed (pruned by --write-baseline): {key}")
    if added or strict:
        print(
            f"test assert level check: {len(added)} new warning-only cases, "
            f"{len(strict)} strict-file violations ({len(known)} baselined, {len(stale)} stale)"
        )
        return 1
    print(
        f"test assert level check OK: no new warning-only cases "
        f"({len(known)} baselined, {len(stale)} stale)"
    )
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
