#!/usr/bin/env python3
"""Inventory C++ copy/temporary candidates; optionally run semantic clang-tidy checks.

No automatic fixes: constructors, overloads, ownership and measured hot paths must
be reviewed before changing a call. A clean report is not proof of optimal code.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

import utf8_stdio

ROOT = Path(__file__).resolve().parents[1]
CHECKS = (
    "-*,performance-*,modernize-use-emplace,modernize-pass-by-value,"
    "bugprone-use-after-move,bugprone-move-forwarding-reference,"
    "misc-noexcept-move-constructor"
)
EXTENSIONS = {".cpp", ".cc", ".cxx", ".h", ".hpp", ".hh", ".hxx"}
# Mask comments and literals before matching; retain offsets for line locations.
TOKENS = re.compile(
    r'//[^\n]*|/\*[\s\S]*?\*/|(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\('
    r'[\s\S]*?\)\1"|(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*"|'
    r"(?:u8|u|U|L)?'(?:\\[\s\S]|[^'\\])*'"
)
RULES = (
    (
        "return-move",
        r"\breturn\s+std\s*::\s*move\s*\(\s*([A-Za-z_]\w*)\s*\)\s*;",
        "Check local/parameter ownership: moving a same-type local can inhibit NRVO. "
        "Members, references and conversions may require the move.",
    ),
    (
        "range-value",
        r"\bfor\s*\(\s*(?:const\s+)?auto\s+(?:[A-Za-z_]\w*|\[[^\]\n]+\])\s*:",
        "Check element type and mutation: a value loop may copy an expensive object. "
        "Scalars, proxies and deliberate snapshots may be appropriate.",
    ),
    (
        "push-temporary",
        r"\.\s*push_back\s*\(\s*(?:std\s*::\s*)?[A-Z]\w*(?:::\w+)*\s*[({]",
        "Check container value type and constructor: emplace_back(args) may avoid a "
        "temporary. Preserve initializer-list, explicit-constructor and exception semantics.",
    ),
    (
        "owning-substr",
        r"\.\s*substr\s*\(",
        "Check receiver and callee types: std::string::substr allocates; a string_view "
        "slice may suffice if the owner outlives its use. string_view::substr is already cheap.",
    ),
)


def mask_cpp(source: str) -> str:
    """Replace comments/literals with spaces while retaining newlines and offsets."""
    return TOKENS.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), source)


def scan(path: Path, root: Path) -> list[dict]:
    source = path.read_text(encoding="utf-8")
    masked = mask_cpp(source)
    lines = source.splitlines()
    findings = []
    for rule, pattern, advice in RULES:
        for match in re.finditer(pattern, masked):
            line = source.count("\n", 0, match.start()) + 1
            findings.append(
                {
                    "file": path.relative_to(root).as_posix(),
                    "line": line,
                    "column": match.start() - source.rfind("\n", 0, match.start()),
                    "rule": rule,
                    "confidence": "review-required",
                    "source": lines[line - 1].strip(),
                    "advice": advice,
                }
            )
    return findings


def tracked_sources(root: Path, paths: list[str]) -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "--", *paths],
        cwd=root,
        capture_output=True,
        encoding="utf-8",
        check=True,
    )
    return sorted(
        {
            root / name
            for name in result.stdout.split("\0")
            if name and Path(name).suffix in EXTENSIONS and (root / name).is_file()
        }
    )


def parse_diagnostics(output: str, root: Path) -> list[dict]:
    """Extract stable source locations; leave context and notes in the raw log."""
    findings = []
    pattern = r"^(.+?):(\d+):(\d+): (warning|error): (.*?) \[([^\]]+)\]$"
    for match in re.finditer(pattern, output, re.MULTILINE):
        file, line, column, severity, message, check = match.groups()
        path = Path(file)
        if not path.is_absolute():
            path = root / path
        try:
            file = path.resolve().relative_to(root.resolve()).as_posix()
        except ValueError:
            continue
        if not file.startswith("src/"):
            continue
        findings.append(
            {
                "file": file,
                "line": int(line),
                "column": int(column),
                "severity": severity,
                "rule": check,
                "message": message,
            }
        )
    return findings


def run_tidy(
    root: Path,
    sources: list[Path],
    database: Path,
    executable: str,
    jobs: int = 1,
    log_dir: Path | None = None,
    compiled_only: bool = False,
    extra_args: tuple[str, ...] = (),
) -> dict:
    """Analyze selected translation units; keep failures separate from findings."""
    entries = json.loads((database / "compile_commands.json").read_text(encoding="utf-8"))
    units = set()
    msvc_units = set()
    for entry in entries:
        path = Path(entry["file"])
        if not path.is_absolute():
            path = Path(entry["directory"]) / path
        units.add(path.resolve())
        command = entry.get("command", " ".join(entry.get("arguments", [])))
        if "msvc-cl.cmd" in command:
            # CMake uses a UTF-8 wrapper. Clang cannot infer cl-mode from .cmd.
            msvc_units.add(path.resolve())
    effective_database = database
    if msvc_units:
        # Clang treats /external:I as -isystem, moving a duplicated -I root
        # behind all regular -I roots. MSVC searches it at its original position.
        # Keep the native search order (notably bundled GLM before Vulkan SDK
        # GLM) in a derived analysis database; never rewrite the build database.
        effective_database = database / "cpp-call-audit-db"
        effective_database.mkdir(parents=True, exist_ok=True)
        normalized_entries = []
        for entry in entries:
            normalized = dict(entry)
            if "command" in normalized and "msvc-cl.cmd" in normalized["command"]:
                normalized["command"] = normalized["command"].replace("/external:I", "-I")
            if "arguments" in normalized and any(
                "msvc-cl.cmd" in arg for arg in normalized["arguments"]
            ):
                normalized["arguments"] = [
                    arg.replace("/external:I", "-I") if arg.startswith("/external:I") else arg
                    for arg in normalized["arguments"]
                ]
            normalized_entries.append(normalized)
        (effective_database / "compile_commands.json").write_text(
            json.dumps(normalized_entries, indent=2) + "\n", encoding="utf-8"
        )
    selected = [p for p in sources if p.suffix in {".cpp", ".cc", ".cxx"}]
    missing = [p.relative_to(root).as_posix() for p in selected if p.resolve() not in units]
    diagnostics = []
    failures = []
    if log_dir:
        log_dir.mkdir(parents=True, exist_ok=True)

    def analyze(path: Path) -> dict:
        result = subprocess.run(
            [
                executable,
                str(path),
                "-p",
                str(effective_database),
                f"--checks={CHECKS}",
                "--header-filter=.*[/\\\\]src[/\\\\].*",
                "--use-color=false",
                "--quiet",
                *[f"--extra-arg={arg}" for arg in extra_args],
                *(
                    ["--extra-arg-before=--driver-mode=cl", "--extra-arg=/WX-"]
                    if path.resolve() in msvc_units
                    else []
                ),
            ],
            cwd=root,
            capture_output=True,
            encoding="utf-8",
            errors="replace",
            check=False,
        )
        diagnostic = {
            "translation_unit": path.relative_to(root).as_posix(),
            "exit_code": result.returncode,
            "output": result.stdout + result.stderr,
        }
        if log_dir:
            key = hashlib.sha256(diagnostic["translation_unit"].encode("utf-8")).hexdigest()[:16]
            (log_dir / f"{key}.json").write_text(
                json.dumps(diagnostic, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
            )
        return diagnostic

    runnable = [path for path in selected if path.resolve() in units]
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(analyze, path) for path in runnable]
        for future in as_completed(futures):
            diagnostic = future.result()
            diagnostics.append(diagnostic)
            if diagnostic["exit_code"]:
                failures.append(diagnostic["translation_unit"])
            print(
                f"clang-tidy [{len(diagnostics)}/{len(runnable)}]: "
                f"{diagnostic['translation_unit']} (exit {diagnostic['exit_code']})",
                flush=True,
            )
    unique = {}
    for diagnostic in diagnostics:
        for finding in parse_diagnostics(diagnostic["output"], root):
            key = (
                finding["file"],
                finding["line"],
                finding["column"],
                finding["rule"],
                finding["message"],
            )
            if key not in unique:
                unique[key] = dict(finding, translation_units=[], verified_translation_units=[])
            unique[key]["translation_units"].append(diagnostic["translation_unit"])
            if diagnostic["exit_code"] == 0:
                unique[key]["verified_translation_units"].append(diagnostic["translation_unit"])
    return {
        "checks": CHECKS,
        "extra_args": list(extra_args),
        "database": str(database),
        "effective_database": str(effective_database),
        "database_sha256": hashlib.sha256(
            (database / "compile_commands.json").read_bytes()
        ).hexdigest(),
        "analysis_adjustments": "The msvc-cl.cmd wrapper uses --driver-mode=cl, /WX-, and /external:I "
        "normalized to -I to preserve MSVC header search order for analysis only; "
        "compiler warnings remain visible and genuine parse errors still fail the unit.",
        "diagnostics": sorted(diagnostics, key=lambda d: d["translation_unit"]),
        "findings": [unique[key] for key in sorted(unique)],
        "failed_units": sorted(failures),
        "missing_units": [] if compiled_only else missing,
        "excluded_units": missing if compiled_only else [],
        "analyzed_units": len(diagnostics),
        "successful_units": len(diagnostics) - len(failures),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "paths",
        nargs="*",
        default=["src/engine", "src/modules"],
        help="Git pathspecs; default: src/engine src/modules",
    )
    parser.add_argument("--output", type=Path, default=Path("build/cpp-call-audit.json"))
    parser.add_argument("--tidy", action="store_true", help="Also run semantic analysis")
    parser.add_argument("--build-dir", type=Path, help="Directory containing compile_commands.json")
    parser.add_argument("--clang-tidy", default="clang-tidy")
    parser.add_argument("--jobs", type=int, default=4, help="Concurrent clang-tidy processes")
    parser.add_argument("--log-dir", type=Path, help="Save each completed TU immediately")
    parser.add_argument(
        "--extra-arg", action="append", default=[], help="Explicit analysis compiler argument"
    )
    parser.add_argument(
        "--compiled-only",
        action="store_true",
        help="Analyze the configured target; list files absent from its database as excluded",
    )
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    if args.tidy and (
        not args.build_dir or not (args.build_dir / "compile_commands.json").is_file()
    ):
        parser.error("--tidy requires --build-dir containing compile_commands.json")
    if args.tidy and not shutil.which(args.clang_tidy):
        parser.error("clang-tidy executable not found")
    sources = tracked_sources(ROOT, args.paths)
    if not sources:
        parser.error("No tracked C++ sources matched the supplied pathspecs")
    findings = sorted(
        (f for p in sources for f in scan(p, ROOT)), key=lambda f: (f["file"], f["line"], f["rule"])
    )
    report = {
        "schema": "evengine.cpp-call-audit",
        "version": 1,
        "unknown_fields": "ignore",
        "scanned_files": len(sources),
        "scope": args.paths,
        "candidates": findings,
        "limitations": "Lexical candidates require type/lifetime review. "
        "Macros and inactive branches are not evaluated. No hot-path or allocation "
        "cost is proven. Semantic analysis requires a complete, usable compilation database.",
    }
    if args.tidy:
        report["clang_tidy"] = run_tidy(
            ROOT,
            sources,
            args.build_dir.resolve(),
            args.clang_tidy,
            args.jobs,
            args.log_dir,
            args.compiled_only,
            tuple(args.extra_arg),
        )
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    for f in findings:
        print(f"{f['file']}:{f['line']}:{f['column']}: [{f['rule']}] {f['source']}")
    print(f"Scanned {len(sources)} files; {len(findings)} review candidates. Report: {args.output}")
    if args.tidy:
        tidy = report["clang_tidy"]
        print(
            f"Semantic analysis: {tidy['analyzed_units']} units; "
            f"{len(tidy['failed_units'])} failed; {len(tidy['missing_units'])} missing."
        )
        if tidy["failed_units"] or tidy["missing_units"] or not tidy["analyzed_units"]:
            return 2
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
