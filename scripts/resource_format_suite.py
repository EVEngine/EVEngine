#!/usr/bin/env python3
"""Inventory resource tests; optionally run exact process-isolated CTest cases.

Source discovery is evidence of test existence, never evidence of format support
or successful execution. Missing CTest registrations fail execution closed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
FAMILIES = {
    "image": ["resource_format_image.cpp", "image.cpp", "image_script.cpp", "asset_graphics_loader.cpp"],
    "sound": ["resource_decoder_lifetime.cpp", "resource_format_sound.cpp", "sound.cpp", "audio.cpp", "editor_audio_import_diagnostics.cpp"],
    "model": ["resource_format_model.cpp", "model3d.cpp", "model3d_normals.cpp", "medialoader_model_link.cpp", "avatar_vrm_vulkan.cpp"],
    "font": ["font.cpp", "graphics_font.cpp"],
    "map": ["map.cpp", "resource_format_map.cpp"],
    "asset": [p.name for p in sorted((ROOT / "test").glob("asset_*.cpp"))],
    "resource": ["filesystem_cpp.cpp", "resource.cpp", "resource_uri.cpp", "resource_lifetime.cpp"],
}


def inventory(root=ROOT):
    result = {}
    for family, files in FAMILIES.items():
        cases = {}
        for name in files:
            path = root / "test" / name
            for case in re.findall(r'TEST_CASE\(\s*"([^"\n]+)"', path.read_text(encoding="utf-8-sig")):
                if case in cases:
                    raise ValueError("duplicate test: " + case)
                cases[case] = "test/" + name
        extensions = []
        loader = {"image": "image/ImageLoader.cpp", "sound": "sound/SoundLoader.cpp", "model": "model3d/ModelLoader.cpp"}.get(family)
        if loader:
            extensions = sorted(set(re.findall(r'ext == "(\.[^"]+)"', (root / "src/modules" / loader).read_text())))
        result[family] = {"advertised_resource_extensions": extensions, "tests": cases}
    return result


def fixture_errors(root=ROOT):
    directory = root / "test/fixtures/resource_formats"
    manifest = json.loads((directory / "manifest.json").read_text())
    if manifest.get("schema") != "evengine.resource-format-fixtures" or manifest.get("version") != 1:
        raise ValueError("unsupported fixture manifest schema/version")
    errors = []
    for name, expected in manifest["sha256"].items():
        if Path(name).name != name:
            raise ValueError("fixture path must be a basename")
        path = directory / name
        if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
            errors.append(name)
    return errors


def format_coverage(groups, results, root=ROOT):
    contract = json.loads((root / "scripts/resource_format_contracts.json").read_text())
    if (set(contract) != {"schema", "version", "unknownFields", "formats"}
            or contract["schema"] != "evengine.resource-format-contracts"
            or contract["version"] != 1 or contract["unknownFields"] != "reject"):
        raise ValueError("unsupported format contract schema/version or unknown fields")
    known_tests = {name for group in inventory(root).values() for name in group["tests"]}
    statuses = {test["name"]: test["status"] for test in results}
    rows = []
    seen = set()
    for row in contract["formats"]:
        if set(row) != {"id", "family", "extensions", "importTests", "exportTests", "exportMode", "scope"}:
            raise ValueError("invalid format contract fields")
        if row["id"] in seen:
            raise ValueError("duplicate format contract: " + row["id"])
        seen.add(row["id"])
        for name in row["importTests"] + row["exportTests"]:
            if name not in known_tests:
                raise ValueError("stale format test reference: " + name)
        if row["family"] not in groups:
            continue
        entry = dict(row)
        for operation in ["import", "export"]:
            names = row[operation + "Tests"]
            entry[operation + "Status"] = ("coverage-missing" if not names else
                "passed" if all(statuses.get(name) == "passed" for name in names) else
                "failed" if any(name in statuses and statuses[name] != "passed" for name in names) else "not-run")
        rows.append(entry)
    for family, group in groups.items():
        for extension in group["advertised_resource_extensions"]:
            if not any(row["family"] == family and extension in row["extensions"] for row in rows):
                raise ValueError("resource extension missing from contract: " + family + extension)
    return rows


def run_cases(build_dir, selected):
    listing = subprocess.run(["ctest", "--test-dir", str(build_dir), "--show-only=json-v1"],
                             check=True, capture_output=True, text=True, errors="replace")
    registered = {t["name"]: t for t in json.loads(listing.stdout)["tests"]}
    missing = sorted(set(selected) - registered.keys())
    disabled = sorted(name for name in selected if name in registered and any(
        p["name"] == "DISABLED" and p["value"] for p in registered[name].get("properties", [])))
    if missing or disabled or not selected:
        return 1, {"status": "failed", "missing_registrations": missing, "disabled_tests": disabled}
    results = []
    outputs = []
    code = 0
    # Keep the command under Windows' process command-line limit as suites grow.
    with tempfile.TemporaryDirectory(prefix="eve-resource-ctest-") as directory:
        for offset in range(0, len(selected), 40):
            batch = selected[offset:offset + 40]
            junit = Path(directory) / "results.xml"
            junit.unlink(missing_ok=True)
            pattern = "^(" + "|".join(re.escape(name) for name in batch) + ")$"
            run = subprocess.run(["ctest", "--test-dir", str(build_dir), "--output-on-failure",
                                  "--no-tests=error", "--output-junit", str(junit), "-R", pattern],
                                 capture_output=True, text=True, errors="replace")
            outputs.append(run.stdout + run.stderr)
            code = code or run.returncode
            if not junit.is_file():
                code = code or 1
                results.extend({"name": name, "status": "missing-result"} for name in batch)
                continue
            observed = set()
            for case in ET.parse(junit).getroot().iter("testcase"):
                name = case.attrib["name"]
                status = "passed"
                if (case.find("failure") is not None or case.find("error") is not None
                        or "Assertion Failed" in case.findtext("system-out", "")):
                    status = "failed"
                elif case.find("skipped") is not None or case.get("status") in ("notrun", "disabled"):
                    status = "skipped"
                if name not in batch or name in observed:
                    status = "unexpected-result"
                observed.add(name)
                result = {"name": name, "status": status, "seconds": case.get("time")}
                if status != "passed":
                    result["output"] = case.findtext("system-out", "")[:16000]
                results.append(result)
                if status != "passed":
                    code = code or 1
            for name in sorted(set(batch) - observed):
                results.append({"name": name, "status": "missing-result"})
                code = code or 1
    return code, {"status": "passed" if code == 0 else "failed", "results": results,
                  "output": "\n".join(outputs)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, help="Run tests from this configured CTest build")
    parser.add_argument("--family", choices=sorted(FAMILIES), action="append")
    parser.add_argument("--new-contracts-only", action="store_true", help="Select resourceFormats.* cases only")
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    groups = {k: v for k, v in inventory().items() if not args.family or k in args.family}
    if args.new_contracts_only:
        for group in groups.values():
            group["tests"] = {k: v for k, v in group["tests"].items() if k.startswith("resourceFormats.")}
    report = {"schema": "evengine.resource-test-report", "version": 1,
              "status": "not-run", "families": groups,
              "limitation": "Extension routing and test names do not prove per-format coverage or export support."}
    code = 0
    try:
        report["fixture_errors"] = fixture_errors()
        if report["fixture_errors"]:
            code, report["status"] = 1, "failed"
        elif args.build_dir:
            selected = sorted({name for group in groups.values() for name in group["tests"]})
            code, result = run_cases(args.build_dir, selected)
            report.update(result)
    except (subprocess.SubprocessError, OSError, ValueError, KeyError, ET.ParseError) as error:
        report.update(status="failed", error=str(error))
        code = 1
    try:
        report["format_coverage"] = format_coverage(groups, report.get("results", []))
    except (OSError, ValueError, KeyError) as error:
        report.update(status="failed", error=str(error))
        code = 1
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(f"{report['status']}: {sum(len(g['tests']) for g in groups.values())} source tests; {args.report}")
    if report.get("error"):
        print(report["error"], file=sys.stderr)
    if report.get("fixture_errors"):
        print("Fixture hash mismatch: " + ", ".join(report["fixture_errors"]), file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main())
