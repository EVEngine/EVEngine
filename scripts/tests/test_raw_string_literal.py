"""Regression tests for the embedded-script raw-string literal generator.

The engine embeds src/scripts/*.nut and *.glsl into a generated C++ file as
raw-string literals. A fixed R"(...)" delimiter would break the build the day
a script contained the terminator sequence )"; the generator (see
src/scripts/raw_string_literal.cmake) picks a per-content delimiter that is
guaranteed absent, so embedding is always safe.
"""

import pathlib
import re
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
RAW_LITERAL = ROOT / "src" / "scripts" / "raw_string_literal.cmake"


def run_cmake(harness: pathlib.Path, out: pathlib.Path) -> None:
    subprocess.run(
        [
            "cmake",
            f"-DOUT={out.as_posix()}",
            f"-DRAW_LITERAL={RAW_LITERAL.as_posix()}",
            "-P",
            harness.as_posix(),
        ],
        check=True,
        capture_output=True,
        text=True,
    )


def roundtrip(literal: str) -> str:
    """Extract the content embedded in a C++ raw-string literal."""
    m = re.match(r'^R"([A-Za-z_][A-Za-z0-9_]*)\((.*)\)\1"$', literal, re.S)
    assert m, f"literal is not a valid raw string: {literal!r}"
    delim, content = m.group(1), m.group(2)
    assert f"){delim}\"" not in content, "terminator leaked into content"
    return content


class RawStringLiteralTest(unittest.TestCase):
    def test_delimiter_selection_and_roundtrip(self):
        cases = {
            # Plain script: default delimiter.
            "plain": 'print("hi")\nlocal x = 1; // comment\n',
            # The old landmine: )" inside the source.
            "terminator": 'ui.button("follow (1)", "m_follow");\n',
            # Both the default and a grown delimiter present.
            "grown": 'a = ")eve"\nb = ")"\nc = "x)evexx"\n',
            # Everything hostile at once.
            "hostile": 's = "a\\b\\\"c"\n@dollar@ ${braces} )evex" )"\n//\u4e2d\u6587\n',
            # Empty source (EVENGINE_BUILD_DEMO=OFF embeds an empty demo).
            "empty": "",
        }

        with tempfile.TemporaryDirectory() as tmp:
            tmp = pathlib.Path(tmp)
            harness = tmp / "harness.cmake"
            out = tmp / "out.txt"
            lines = ['include("${RAW_LITERAL}")', 'set(_out "")']
            for idx, (name, content) in enumerate(cases.items(), start=1):
                # CMake does not interpret \n escapes in quoted strings, so
                # inject multi-line content verbatim via a bracket argument.
                lines.append(f"set(_content_{idx} [====[\n{content}]====])")
                lines.append(
                    f"eve_raw_string_literal(_lit_{idx} \"${{_content_{idx}}}\")"
                )
                lines.append(f'string(APPEND _out "CASE{idx}|${{_lit_{idx}}}\\n")')
            lines.append('file(WRITE "${OUT}" "${_out}")')
            harness.write_text("\n".join(lines) + "\n", encoding="utf-8")

            run_cmake(harness, out)
            results = out.read_text(encoding="utf-8")
            markers = list(re.finditer(r"CASE(\d+)\|", results))
            for idx, (name, content) in enumerate(cases.items(), start=1):
                m = markers[idx - 1]
                end = markers[idx].start() if idx < len(markers) else len(results)
                literal = results[m.end() : end].rstrip("\n")
                self.assertEqual(roundtrip(literal), content, f"roundtrip failed: {name}")


if __name__ == "__main__":
    unittest.main()
